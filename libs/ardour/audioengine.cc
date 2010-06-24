/*
    Copyright (C) 2002 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <unistd.h>
#include <cerrno>
#include <vector>
#include <exception>
#include <stdexcept>
#include <sstream>

#include <glibmm/timer.h>
#include <jack/jack.h>
#include <jack/thread.h>

#include "pbd/pthread_utils.h"
#include "pbd/stacktrace.h"
#include "pbd/unknown_type.h"

#include "midi++/jack.h"

#include "ardour/amp.h"
#include "ardour/audio_port.h"
#include "ardour/audioengine.h"
#include "ardour/buffer.h"
#include "ardour/buffer_set.h"
#include "ardour/cycle_timer.h"
#include "ardour/delivery.h"
#include "ardour/event_type_map.h"
#include "ardour/internal_return.h"
#include "ardour/io.h"
#include "ardour/meter.h"
#include "ardour/midi_port.h"
#include "ardour/process_thread.h"
#include "ardour/port.h"
#include "ardour/port_set.h"
#include "ardour/session.h"
#include "ardour/timestamps.h"
#include "ardour/utils.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

gint AudioEngine::m_meter_exit;
AudioEngine* AudioEngine::_instance = 0;

#define GET_PRIVATE_JACK_POINTER(j)  jack_client_t* _priv_jack = (jack_client_t*) (j); if (!_priv_jack) { return; }
#define GET_PRIVATE_JACK_POINTER_RET(j,r) jack_client_t* _priv_jack = (jack_client_t*) (j); if (!_priv_jack) { return r; }

AudioEngine::AudioEngine (string client_name, string session_uuid)
	: ports (new Ports)
{
	_instance = this; /* singleton */

	session_remove_pending = false;
	_running = false;
	_has_run = false;
	last_monitor_check = 0;
	monitor_check_interval = max_frames;
	_processed_frames = 0;
	_usecs_per_cycle = 0;
	_jack = 0;
	_frame_rate = 0;
	_buffer_size = 0;
	_freewheeling = false;
        _main_thread = 0;

	m_meter_thread = 0;
	g_atomic_int_set (&m_meter_exit, 0);

	if (connect_to_jack (client_name, session_uuid)) {
		throw NoBackendAvailable ();
	}

	Port::set_engine (this);

	// Initialize parameter metadata (e.g. ranges)
	Evoral::Parameter p(NullAutomation);
	p = EventTypeMap::instance().new_parameter(NullAutomation);
	p = EventTypeMap::instance().new_parameter(GainAutomation);
	p = EventTypeMap::instance().new_parameter(PanAutomation);
	p = EventTypeMap::instance().new_parameter(PluginAutomation);
	p = EventTypeMap::instance().new_parameter(SoloAutomation);
	p = EventTypeMap::instance().new_parameter(MuteAutomation);
	p = EventTypeMap::instance().new_parameter(MidiCCAutomation);
	p = EventTypeMap::instance().new_parameter(MidiPgmChangeAutomation);
	p = EventTypeMap::instance().new_parameter(MidiPitchBenderAutomation);
	p = EventTypeMap::instance().new_parameter(MidiChannelPressureAutomation);
	p = EventTypeMap::instance().new_parameter(FadeInAutomation);
	p = EventTypeMap::instance().new_parameter(FadeOutAutomation);
	p = EventTypeMap::instance().new_parameter(EnvelopeAutomation);
	p = EventTypeMap::instance().new_parameter(MidiCCAutomation);
}

AudioEngine::~AudioEngine ()
{
	{
		Glib::Mutex::Lock tm (_process_lock);
		session_removed.signal ();

		if (_running) {
			jack_client_close (_jack);
			_jack = 0;
		}

		stop_metering_thread ();
	}
}

jack_client_t*
AudioEngine::jack() const
{
	return _jack;
}

void
_thread_init_callback (void * /*arg*/)
{
	/* make sure that anybody who needs to know about this thread
	   knows about it.
	*/

	pthread_set_name (X_("audioengine"));

	PBD::notify_gui_about_thread_creation ("gui", pthread_self(), X_("Audioengine"), 4096);
	PBD::notify_gui_about_thread_creation ("midiui", pthread_self(), X_("Audioengine"), 128);

	SessionEvent::create_per_thread_pool (X_("Audioengine"), 512);

	MIDI::JACK_MidiPort::set_process_thread (pthread_self());
}

typedef void (*_JackInfoShutdownCallback)(jack_status_t code, const char* reason, void *arg);

static void (*on_info_shutdown)(jack_client_t*, _JackInfoShutdownCallback, void *);
extern void jack_on_info_shutdown (jack_client_t*, _JackInfoShutdownCallback, void *) __attribute__((weak));

static void check_jack_symbols () __attribute__((constructor));

void check_jack_symbols ()
{
       /* use weak linking to see if we really have various late-model JACK function */
       on_info_shutdown = jack_on_info_shutdown;
}

static void
ardour_jack_error (const char* msg)
{
	error << "JACK: " << msg << endmsg;
}

int
AudioEngine::start ()
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);

	if (!_running) {

		nframes_t blocksize = jack_get_buffer_size (_priv_jack);

		if (_session) {
			BootMessage (_("Connect session to engine"));

			_session->set_block_size (blocksize);
			_session->set_frame_rate (jack_get_sample_rate (_priv_jack));

			/* page in as much of the session process code as we
			   can before we really start running.
			*/

			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
			_session->process (blocksize);
		}

		_processed_frames = 0;
		last_monitor_check = 0;

                if (on_info_shutdown) {
                        jack_on_info_shutdown (_priv_jack, halted_info, this);
                } else {
                        jack_on_shutdown (_priv_jack, halted, this);
                }
		jack_set_graph_order_callback (_priv_jack, _graph_order_callback, this);
		jack_set_thread_init_callback (_priv_jack, _thread_init_callback, this);
		// jack_set_process_callback (_priv_jack, _process_callback, this);
		jack_set_process_thread (_priv_jack, _process_thread, this);
		jack_set_sample_rate_callback (_priv_jack, _sample_rate_callback, this);
		jack_set_buffer_size_callback (_priv_jack, _bufsize_callback, this);
		jack_set_xrun_callback (_priv_jack, _xrun_callback, this);
#ifdef HAVE_JACK_SESSION 
		if( jack_set_session_callback )
		    jack_set_session_callback (_priv_jack, _session_callback, this);
#endif
		jack_set_sync_callback (_priv_jack, _jack_sync_callback, this);
		jack_set_freewheel_callback (_priv_jack, _freewheel_callback, this);
		jack_set_port_registration_callback (_priv_jack, _registration_callback, this);
		jack_set_port_connect_callback (_priv_jack, _connect_callback, this);

		if (_session && _session->config.get_jack_time_master()) {
			jack_set_timebase_callback (_priv_jack, 0, _jack_timebase_callback, this);
		}

		jack_set_error_function (ardour_jack_error);

		if (jack_activate (_priv_jack) == 0) {
			_running = true;
			_has_run = true;
			Running(); /* EMIT SIGNAL */
		} else {
			// error << _("cannot activate JACK client") << endmsg;
		}

		_raw_buffer_sizes[DataType::AUDIO] = blocksize * sizeof(float);

                jack_port_t* midi_port = jack_port_register (_priv_jack, "m", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
                if (!midi_port) {
                        error << _("Cannot create temporary MIDI port to determine MIDI buffer size") << endmsg;
                } else {
                        _raw_buffer_sizes[DataType::MIDI] = jack_midi_max_event_size (jack_port_get_buffer(midi_port, blocksize));
                        cerr << "MIDI port buffers = " << _raw_buffer_sizes[DataType::MIDI] << endl;
                        jack_port_unregister (_priv_jack, midi_port);
                }
	}

	return _running ? 0 : -1;
}

int
AudioEngine::stop (bool forever)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);

	if (_priv_jack) {
		if (forever) {
			disconnect_from_jack ();
		} else {
			jack_deactivate (_priv_jack);
			Stopped(); /* EMIT SIGNAL */
			MIDI::JACK_MidiPort::JackHalted (); /* EMIT SIGNAL */
		}
	}

	return _running ? -1 : 0;
}


bool
AudioEngine::get_sync_offset (nframes_t& offset) const
{

#ifdef HAVE_JACK_VIDEO_SUPPORT

	GET_PRIVATE_JACK_POINTER_RET (_jack, false);

	jack_position_t pos;

	if (_priv_jack) {
		(void) jack_transport_query (_priv_jack, &pos);

		if (pos.valid & JackVideoFrameOffset) {
			offset = pos.video_offset;
			return true;
		}
	}
#else
	/* keep gcc happy */
	offset = 0;
#endif

	return false;
}

void
AudioEngine::_jack_timebase_callback (jack_transport_state_t state, nframes_t nframes,
				      jack_position_t* pos, int new_position, void *arg)
{
	static_cast<AudioEngine*> (arg)->jack_timebase_callback (state, nframes, pos, new_position);
}

void
AudioEngine::jack_timebase_callback (jack_transport_state_t state, nframes_t nframes,
				     jack_position_t* pos, int new_position)
{
	if (_jack && _session && _session->synced_to_jack()) {
		_session->jack_timebase_callback (state, nframes, pos, new_position);
	}
}

int
AudioEngine::_jack_sync_callback (jack_transport_state_t state, jack_position_t* pos, void* arg)
{
	return static_cast<AudioEngine*> (arg)->jack_sync_callback (state, pos);
}

int
AudioEngine::jack_sync_callback (jack_transport_state_t state, jack_position_t* pos)
{
	if (_jack && _session) {
		return _session->jack_sync_callback (state, pos);
	}

	return true;
}

int
AudioEngine::_xrun_callback (void *arg)
{
	AudioEngine* ae = static_cast<AudioEngine*> (arg);
	if (ae->connected()) {
		ae->Xrun (); /* EMIT SIGNAL */
	}
	return 0;
}

#ifdef HAVE_JACK_SESSION
void
AudioEngine::_session_callback (jack_session_event_t *event, void *arg)
{
	printf( "helo.... " );
	AudioEngine* ae = static_cast<AudioEngine*> (arg);
	if (ae->connected()) {
		ae->JackSessionEvent ( event ); /* EMIT SIGNAL */
	}
}
#endif
int
AudioEngine::_graph_order_callback (void *arg)
{
	AudioEngine* ae = static_cast<AudioEngine*> (arg);
	if (ae->connected()) {
		ae->GraphReordered (); /* EMIT SIGNAL */
	}
	return 0;
}

/** Wrapped which is called by JACK as its process callback.  It is just
 * here to get us back into C++ land by calling AudioEngine::process_callback()
 * @param nframes Number of frames passed by JACK.
 * @param arg User argument passed by JACK, which will be the AudioEngine*.
 */
int
AudioEngine::_process_callback (nframes_t nframes, void *arg)
{
	return static_cast<AudioEngine *> (arg)->process_callback (nframes);
}

void*
AudioEngine::_process_thread (void *arg)
{
	return static_cast<AudioEngine *> (arg)->process_thread ();
}

void
AudioEngine::_freewheel_callback (int onoff, void *arg)
{
	static_cast<AudioEngine*>(arg)->_freewheeling = onoff;
}

void
AudioEngine::_registration_callback (jack_port_id_t /*id*/, int /*reg*/, void* arg)
{
	AudioEngine* ae = static_cast<AudioEngine*> (arg);
	ae->PortRegisteredOrUnregistered (); /* EMIT SIGNAL */
}

void
AudioEngine::_connect_callback (jack_port_id_t /*id_a*/, jack_port_id_t /*id_b*/, int /*conn*/, void* arg)
{
	AudioEngine* ae = static_cast<AudioEngine*> (arg);
	ae->PortConnectedOrDisconnected (); /* EMIT SIGNAL */
}

void
AudioEngine::split_cycle (nframes_t offset)
{
	/* caller must hold process lock */

	Port::increment_port_offset (offset);

	/* tell all Ports that we're going to start a new (split) cycle */

	boost::shared_ptr<Ports> p = ports.reader();

	for (Ports::iterator i = p->begin(); i != p->end(); ++i) {
		(*i)->cycle_split ();
	}
}

void
AudioEngine::finish_process_cycle (int status)
{
        GET_PRIVATE_JACK_POINTER(_jack);
        jack_cycle_signal (_jack, 0);
}

void*
AudioEngine::process_thread ()
{
        /* JACK doesn't do this for us when we use the wait API 
         */

        _thread_init_callback (0);

        _main_thread = new ProcessThread;

        while (1) {
                GET_PRIVATE_JACK_POINTER_RET(_jack,0);
                jack_nframes_t nframes = jack_cycle_wait (_jack);

                if (process_callback (nframes)) {
                        cerr << "--- process\n";
                        return 0;
                }

                finish_process_cycle (0);
        }

        return 0;
}

/** Method called by JACK (via _process_callback) which says that there
 * is work to be done.
 * @param nframes Number of frames to process.
 */
int
AudioEngine::process_callback (nframes_t nframes)
{
	GET_PRIVATE_JACK_POINTER_RET(_jack,0);
	// CycleTimer ct ("AudioEngine::process");
	Glib::Mutex::Lock tm (_process_lock, Glib::TRY_LOCK);

	/// The number of frames that will have been processed when we've finished
	nframes_t next_processed_frames;

	/* handle wrap around of total frames counter */

	if (max_frames - _processed_frames < nframes) {
		next_processed_frames = nframes - (max_frames - _processed_frames);
	} else {
		next_processed_frames = _processed_frames + nframes;
	}

	if (!tm.locked() || _session == 0) {
		/* return having done nothing */
		_processed_frames = next_processed_frames;
		return 0;
	}

	if (session_remove_pending) {
		/* perform the actual session removal */
		_session = 0;
		session_remove_pending = false;
		session_removed.signal();
		_processed_frames = next_processed_frames;
		return 0;
	}

	/* tell all relevant objects that we're starting a new cycle */

	Delivery::CycleStart (nframes);
	Port::set_port_offset (0);
	InternalReturn::CycleStart (nframes);

	/* tell all Ports that we're starting a new cycle */

	boost::shared_ptr<Ports> p = ports.reader();

	for (Ports::iterator i = p->begin(); i != p->end(); ++i) {
		(*i)->cycle_start (nframes);
	}

	if (_freewheeling) {
		/* emit the Freewheel signal and stop freewheeling in the event of trouble 
		 */
                boost::optional<int> r = Freewheel (nframes);
		if (r.get_value_or (0)) {
			jack_set_freewheel (_priv_jack, false);
		}

	} else {
		if (_session) {
			_session->process (nframes);

		}
	}

	if (_freewheeling) {
		return 0;
	}

	if (!_running) {
		_processed_frames = next_processed_frames;
		return 0;
	}

	if (last_monitor_check + monitor_check_interval < next_processed_frames) {

		boost::shared_ptr<Ports> p = ports.reader();

		for (Ports::iterator i = p->begin(); i != p->end(); ++i) {

			Port *port = (*i);
			bool x;

			if (port->_last_monitor != (x = port->monitoring_input ())) {
				port->_last_monitor = x;
				/* XXX I think this is dangerous, due to
				   a likely mutex in the signal handlers ...
				*/
				 port->MonitorInputChanged (x); /* EMIT SIGNAL */
			}
		}
		last_monitor_check = next_processed_frames;
	}

	if (_session->silent()) {

		boost::shared_ptr<Ports> p = ports.reader();

		for (Ports::iterator i = p->begin(); i != p->end(); ++i) {

			Port *port = (*i);

			if (port->sends_output()) {
				port->get_buffer(nframes).silence(nframes);
			}
		}
	}

	// Finalize ports

	for (Ports::iterator i = p->begin(); i != p->end(); ++i) {
		(*i)->cycle_end (nframes);
	}

	_processed_frames = next_processed_frames;
	return 0;
}

int
AudioEngine::_sample_rate_callback (nframes_t nframes, void *arg)
{
	return static_cast<AudioEngine *> (arg)->jack_sample_rate_callback (nframes);
}

int
AudioEngine::jack_sample_rate_callback (nframes_t nframes)
{
	_frame_rate = nframes;
	_usecs_per_cycle = (int) floor ((((double) frames_per_cycle() / nframes)) * 1000000.0);

	/* check for monitor input change every 1/10th of second */

	monitor_check_interval = nframes / 10;
	last_monitor_check = 0;

	if (_session) {
		_session->set_frame_rate (nframes);
	}

	SampleRateChanged (nframes); /* EMIT SIGNAL */

	return 0;
}

int
AudioEngine::_bufsize_callback (nframes_t nframes, void *arg)
{
	return static_cast<AudioEngine *> (arg)->jack_bufsize_callback (nframes);
}

int
AudioEngine::jack_bufsize_callback (nframes_t nframes)
{
        bool need_midi_size = true;
        bool need_audio_size = true;

	_buffer_size = nframes;
	_usecs_per_cycle = (int) floor ((((double) nframes / frame_rate())) * 1000000.0);
	last_monitor_check = 0;

	boost::shared_ptr<Ports> p = ports.reader();

        /* crude guesses, see below where we try to get the right answers.

           Note that our guess for MIDI deliberatey tries to overestimate
           by a little. It would be nicer if we could get the actual
           size from a port, but we have to use this estimate in the 
           event that there are no MIDI ports currently. If there are
           the value will be adjusted below.
         */

        _raw_buffer_sizes[DataType::AUDIO] = nframes * sizeof (Sample);
        _raw_buffer_sizes[DataType::MIDI] = nframes * 4 - (nframes/2);

	for (Ports::iterator i = p->begin(); i != p->end(); ++i) {

                if (need_audio_size && (*i)->type() == DataType::AUDIO) {
                        _raw_buffer_sizes[DataType::AUDIO] = (*i)->raw_buffer_size (nframes);
                        need_audio_size = false;
                }
                
                        
                if (need_midi_size && (*i)->type() == DataType::MIDI) {
                        _raw_buffer_sizes[DataType::MIDI] = (*i)->raw_buffer_size (nframes);
                        need_midi_size = false;
                }
                
		(*i)->reset();
	}

	if (_session) {
		_session->set_block_size (_buffer_size);
	}

	return 0;
}

void
AudioEngine::stop_metering_thread ()
{
	if (m_meter_thread) {
		g_atomic_int_set (&m_meter_exit, 1);
		m_meter_thread->join ();
		m_meter_thread = 0;
	}
}

void
AudioEngine::start_metering_thread ()
{
	if (m_meter_thread == 0) {
		g_atomic_int_set (&m_meter_exit, 0);
		m_meter_thread = Glib::Thread::create (boost::bind (&AudioEngine::meter_thread, this),
						       500000, true, true, Glib::THREAD_PRIORITY_NORMAL);
	}
}

void
AudioEngine::meter_thread ()
{
	pthread_set_name (X_("meter"));

	while (true) {
		Glib::usleep (10000); /* 1/100th sec interval */
		if (g_atomic_int_get(&m_meter_exit)) {
			break;
		}
		Metering::Meter ();
	}
}

void
AudioEngine::set_session (Session *s)
{
	Glib::Mutex::Lock pl (_process_lock);

	SessionHandlePtr::set_session (s);

	if (_session) {

		start_metering_thread ();
		
		nframes_t blocksize = jack_get_buffer_size (_jack);
		
		/* page in as much of the session process code as we
		   can before we really start running.
		*/
		
		boost::shared_ptr<Ports> p = ports.reader();
		
		for (Ports::iterator i = p->begin(); i != p->end(); ++i) {
			(*i)->cycle_start (blocksize);
		}
		
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		_session->process (blocksize);
		
		for (Ports::iterator i = p->begin(); i != p->end(); ++i) {
			(*i)->cycle_end (blocksize);
		}
	}
}

void
AudioEngine::remove_session ()
{
	Glib::Mutex::Lock lm (_process_lock);

	if (_running) {

		stop_metering_thread ();

		if (_session) {
			session_remove_pending = true;
			session_removed.wait(_process_lock);
		}

	} else {
		SessionHandlePtr::set_session (0);
	}

	remove_all_ports ();
}

void
AudioEngine::port_registration_failure (const std::string& portname)
{
	GET_PRIVATE_JACK_POINTER (_jack);
	string full_portname = jack_client_name;
	full_portname += ':';
	full_portname += portname;


	jack_port_t* p = jack_port_by_name (_priv_jack, full_portname.c_str());
	string reason;

	if (p) {
		reason = string_compose (_("a port with the name \"%1\" already exists: check for duplicated track/bus names"), portname);
	} else {
		reason = string_compose (_("No more JACK ports are available. You will need to stop %1 and restart JACK with ports if you need this many tracks."), PROGRAM_NAME);
	}

	throw PortRegistrationFailure (string_compose (_("AudioEngine: cannot register port \"%1\": %2"), portname, reason).c_str());
}

Port *
AudioEngine::register_port (DataType dtype, const string& portname, bool input)
{
	Port* newport = 0;

	try {
		if (dtype == DataType::AUDIO) {
			newport = new AudioPort (portname, (input ? Port::IsInput : Port::IsOutput));
		} else if (dtype == DataType::MIDI) {
			newport = new MidiPort (portname, (input ? Port::IsInput : Port::IsOutput));
		} else {
			throw PortRegistrationFailure("unable to create port (unknown type)");
		}

		size_t& old_buffer_size  = _raw_buffer_sizes[newport->type()];
		size_t  port_buffer_size = newport->raw_buffer_size(0);
		if (port_buffer_size > old_buffer_size) {
			old_buffer_size = port_buffer_size;
		}

		RCUWriter<Ports> writer (ports);
		boost::shared_ptr<Ports> ps = writer.get_copy ();
		ps->insert (ps->begin(), newport);

		/* writer goes out of scope, forces update */

		return newport;
	}

	catch (PortRegistrationFailure& err) {
		throw err;
	} catch (std::exception& e) {
		throw PortRegistrationFailure(string_compose(
				_("unable to create port: %1"), e.what()).c_str());
	} catch (...) {
		throw PortRegistrationFailure("unable to create port (unknown error)");
	}
}

Port *
AudioEngine::register_input_port (DataType type, const string& portname)
{
	return register_port (type, portname, true);
}

Port *
AudioEngine::register_output_port (DataType type, const string& portname)
{
	return register_port (type, portname, false);
}

int
AudioEngine::unregister_port (Port& port)
{
	/* caller must hold process lock */

	if (!_running) {
		/* probably happening when the engine has been halted by JACK,
		   in which case, there is nothing we can do here.
		   */
		return 0;
	}

	{
		RCUWriter<Ports> writer (ports);
		boost::shared_ptr<Ports> ps = writer.get_copy ();

		for (Ports::iterator i = ps->begin(); i != ps->end(); ++i) {
			if ((*i) == &port) {
				delete *i;
				ps->erase (i);
				break;
			}
		}

		/* writer goes out of scope, forces update */
	}

	return 0;
}

int
AudioEngine::connect (const string& source, const string& destination)
{
	/* caller must hold process lock */

	int ret;

	if (!_running) {
		if (!_has_run) {
			fatal << _("connect called before engine was started") << endmsg;
			/*NOTREACHED*/
		} else {
			return -1;
		}
	}

	string s = make_port_name_non_relative (source);
	string d = make_port_name_non_relative (destination);


	Port* src = get_port_by_name_locked (s);
	Port* dst = get_port_by_name_locked (d);

	if (src) {
		ret = src->connect (d);
	} else if (dst) {
		ret = dst->connect (s);
	} else {
		/* neither port is known to us, and this API isn't intended for use as a general patch bay */
		ret = -1;
	}

	if (ret > 0) {
		/* already exists - no error, no warning */
	} else if (ret < 0) {
		error << string_compose(_("AudioEngine: cannot connect %1 (%2) to %3 (%4)"),
					source, s, destination, d)
		      << endmsg;
	}

	return ret;
}

int
AudioEngine::disconnect (const string& source, const string& destination)
{
	/* caller must hold process lock */

	int ret;

	if (!_running) {
		if (!_has_run) {
			fatal << _("disconnect called before engine was started") << endmsg;
			/*NOTREACHED*/
		} else {
			return -1;
		}
	}

	string s = make_port_name_non_relative (source);
	string d = make_port_name_non_relative (destination);

	Port* src = get_port_by_name_locked (s);
	Port* dst = get_port_by_name_locked (d);

	if (src) {
			ret = src->disconnect (d);
	} else if (dst) {
			ret = dst->disconnect (s);
	} else {
		/* neither port is known to us, and this API isn't intended for use as a general patch bay */
		ret = -1;
	}
	return ret;
}

int
AudioEngine::disconnect (Port& port)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,-1);

	if (!_running) {
		if (!_has_run) {
			fatal << _("disconnect called before engine was started") << endmsg;
			/*NOTREACHED*/
		} else {
			return -1;
		}
	}

	return port.disconnect_all ();
}

ARDOUR::nframes_t
AudioEngine::frame_rate () const
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,0);
	if (_frame_rate == 0) {
	  return (_frame_rate = jack_get_sample_rate (_priv_jack));
	} else {
	  return _frame_rate;
	}
}

size_t
AudioEngine::raw_buffer_size (DataType t)
{
	std::map<DataType,size_t>::const_iterator s = _raw_buffer_sizes.find(t);
	return (s != _raw_buffer_sizes.end()) ? s->second : 0;
}

ARDOUR::nframes_t
AudioEngine::frames_per_cycle () const
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,0);
	if (_buffer_size == 0) {
	  return (_buffer_size = jack_get_buffer_size (_jack));
	} else {
	  return _buffer_size;
	}
}

/** @param name Full name of port (including prefix:)
 *  @return Corresponding Port*, or 0.  This object remains the property of the AudioEngine
 *  so must not be deleted.
 */
Port *
AudioEngine::get_port_by_name (const string& portname)
{
	string s;
	if (portname.find_first_of (':') == string::npos) {
		s = make_port_name_non_relative (portname);
	} else {
		s = portname;
	}

	Glib::Mutex::Lock lm (_process_lock);
	return get_port_by_name_locked (s);
}

Port *
AudioEngine::get_port_by_name_locked (const string& portname)
{
	/* caller must hold process lock */

	if (!_running) {
		if (!_has_run) {
			fatal << _("get_port_by_name_locked() called before engine was started") << endmsg;
			/*NOTREACHED*/
		} else {
			return 0;
		}
	}

	if (portname.substr (0, jack_client_name.length ()) != jack_client_name) {
		/* not an ardour: port */
		return 0;
	}

	std::string const rel = make_port_name_relative (portname);

	boost::shared_ptr<Ports> pr = ports.reader();

	for (Ports::iterator i = pr->begin(); i != pr->end(); ++i) {
		if (rel == (*i)->name()) {
			return *i;
		}
	}

	return 0;
}

const char **
AudioEngine::get_ports (const string& port_name_pattern, const string& type_name_pattern, uint32_t flags)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,0);
	if (!_running) {
		if (!_has_run) {
			fatal << _("get_ports called before engine was started") << endmsg;
			/*NOTREACHED*/
		} else {
			return 0;
		}
	}
	return jack_get_ports (_priv_jack, port_name_pattern.c_str(), type_name_pattern.c_str(), flags);
}

void
AudioEngine::halted_info (jack_status_t code, const char* reason, void *arg)
{
        /* called from jack shutdown handler  */
        
        AudioEngine* ae = static_cast<AudioEngine *> (arg);
        bool was_running = ae->_running;
        
        ae->stop_metering_thread ();
        
        ae->_running = false;
        ae->_buffer_size = 0;
        ae->_frame_rate = 0;
        ae->_jack = 0;
        
        if (was_running) {
#ifdef HAVE_JACK_ON_INFO_SHUTDOWN
                switch (code) {
                case JackBackendError:
                        ae->Halted(reason); /* EMIT SIGNAL */
                        break;
                default:
                        ae->Halted(""); /* EMIT SIGNAL */
                }
#else
                ae->Halted(""); /* EMIT SIGNAL */
#endif
        }
}

void
AudioEngine::halted (void *arg)
{
        cerr << "HALTED by JACK\n";

        /* called from jack shutdown handler  */

	AudioEngine* ae = static_cast<AudioEngine *> (arg);
	bool was_running = ae->_running;

	ae->stop_metering_thread ();

	ae->_running = false;
	ae->_buffer_size = 0;
	ae->_frame_rate = 0;
        ae->_jack = 0;

	if (was_running) {
		ae->Halted(""); /* EMIT SIGNAL */
		MIDI::JACK_MidiPort::JackHalted (); /* EMIT SIGNAL */
	}
}

void
AudioEngine::died ()
{
        /* called from a signal handler for SIGPIPE */

	stop_metering_thread ();

        _running = false;
	_buffer_size = 0;
	_frame_rate = 0;
	_jack = 0;
}

bool
AudioEngine::can_request_hardware_monitoring ()
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,false);
	const char ** ports;

	if ((ports = jack_get_ports (_priv_jack, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortCanMonitor)) == 0) {
		return false;
	}

	free (ports);

	return true;
}


uint32_t
AudioEngine::n_physical_outputs (DataType type) const
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,0);
	const char ** ports;
	uint32_t i = 0;

	if ((ports = jack_get_ports (_priv_jack, NULL, type.to_jack_type(), JackPortIsPhysical|JackPortIsInput)) == 0) {
		return 0;
	}

	for (i = 0; ports[i]; ++i) {}
	free (ports);

	return i;
}

uint32_t
AudioEngine::n_physical_inputs (DataType type) const
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,0);
	const char ** ports;
	uint32_t i = 0;

	if ((ports = jack_get_ports (_priv_jack, NULL, type.to_jack_type(), JackPortIsPhysical|JackPortIsOutput)) == 0) {
		return 0;
	}

	for (i = 0; ports[i]; ++i) {}
	free (ports);

	return i;
}

void
AudioEngine::get_physical_inputs (DataType type, vector<string>& ins)
{
	GET_PRIVATE_JACK_POINTER (_jack);
	const char ** ports;

	if ((ports = jack_get_ports (_priv_jack, NULL, type.to_jack_type(), JackPortIsPhysical|JackPortIsOutput)) == 0) {
		return;
	}

	if (ports) {
		for (uint32_t i = 0; ports[i]; ++i) {
			ins.push_back (ports[i]);
		}
		free (ports);
	}
}

void
AudioEngine::get_physical_outputs (DataType type, vector<string>& outs)
{
	GET_PRIVATE_JACK_POINTER (_jack);
	const char ** ports;
	uint32_t i = 0;

	if ((ports = jack_get_ports (_priv_jack, NULL, type.to_jack_type(), JackPortIsPhysical|JackPortIsInput)) == 0) {
		return;
	}

	for (i = 0; ports[i]; ++i) {
		outs.push_back (ports[i]);
	}
	free (ports);
}

string
AudioEngine::get_nth_physical (DataType type, uint32_t n, int flag)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,"");
	const char ** ports;
	uint32_t i;
	string ret;

	assert(type != DataType::NIL);

	if ((ports = jack_get_ports (_priv_jack, NULL, type.to_jack_type(), JackPortIsPhysical|flag)) == 0) {
		return ret;
	}

	for (i = 0; i < n && ports[i]; ++i) {}

	if (ports[i]) {
		ret = ports[i];
	}

	free ((const char **) ports);

	return ret;
}

void
AudioEngine::update_total_latency (const Port& port)
{
	port.recompute_total_latency ();
}

void
AudioEngine::transport_stop ()
{
	GET_PRIVATE_JACK_POINTER (_jack);
	jack_transport_stop (_priv_jack);
}

void
AudioEngine::transport_start ()
{
	GET_PRIVATE_JACK_POINTER (_jack);
	jack_transport_start (_priv_jack);
}

void
AudioEngine::transport_locate (nframes_t where)
{
	GET_PRIVATE_JACK_POINTER (_jack);
	// cerr << "tell JACK to locate to " << where << endl;
	jack_transport_locate (_priv_jack, where);
}

AudioEngine::TransportState
AudioEngine::transport_state ()
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, ((TransportState) JackTransportStopped));
	jack_position_t pos;
	return (TransportState) jack_transport_query (_priv_jack, &pos);
}

int
AudioEngine::reset_timebase ()
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);
	if (_session) {
		if (_session->config.get_jack_time_master()) {
			return jack_set_timebase_callback (_priv_jack, 0, _jack_timebase_callback, this);
		} else {
			return jack_release_timebase (_jack);
		}
	}
	return 0;
}

int
AudioEngine::freewheel (bool onoff)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);

	if (onoff != _freewheeling) {
                return jack_set_freewheel (_priv_jack, onoff);
                
	} else {
                /* already doing what has been asked for */
                return 0;
	}
}

void
AudioEngine::remove_all_ports ()
{
	/* process lock MUST be held */

	{
		RCUWriter<Ports> writer (ports);
		boost::shared_ptr<Ports> ps = writer.get_copy ();

		for (Ports::iterator i = ps->begin(); i != ps->end(); ++i) {
			delete *i;
		}

		ps->clear ();
	}

	/* clear dead wood list too */

	ports.flush ();
}

int
AudioEngine::connect_to_jack (string client_name, string session_uuid)
{
	jack_options_t options = JackNullOption;
	jack_status_t status;
	const char *server_name = NULL;

	jack_client_name = client_name; /* might be reset below */
#ifdef HAVE_JACK_SESSION
	if (! session_uuid.empty())
	    _jack = jack_client_open (jack_client_name.c_str(), JackSessionID, &status, session_uuid.c_str());
	else
#endif
	    _jack = jack_client_open (jack_client_name.c_str(), options, &status, server_name);

	if (_jack == NULL) {
		// error message is not useful here
		return -1;
	}

	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);

	if (status & JackNameNotUnique) {
		jack_client_name = jack_get_client_name (_priv_jack);
	}

	return 0;
}

int
AudioEngine::disconnect_from_jack ()
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, 0);

	if (_running) {
		stop_metering_thread ();
	}

	{
		Glib::Mutex::Lock lm (_process_lock);
		jack_client_close (_priv_jack);
		_jack = 0;
	}

	_buffer_size = 0;
	_frame_rate = 0;
	_raw_buffer_sizes.clear();

	if (_running) {
		_running = false;
		Stopped(); /* EMIT SIGNAL */
		MIDI::JACK_MidiPort::JackHalted (); /* EMIT SIGNAL */
	}

	return 0;
}

int
AudioEngine::reconnect_to_jack ()
{
	if (_running) {
		disconnect_from_jack ();
		/* XXX give jackd a chance */
		Glib::usleep (250000);
	}

	if (connect_to_jack (jack_client_name, "")) {
		error << _("failed to connect to JACK") << endmsg;
		return -1;
	}

	Ports::iterator i;

	boost::shared_ptr<Ports> p = ports.reader ();

	for (i = p->begin(); i != p->end(); ++i) {
		if ((*i)->reestablish ()) {
			break;
		}
	}

	if (i != p->end()) {
		/* failed */
		remove_all_ports ();
		return -1;
	}

	GET_PRIVATE_JACK_POINTER_RET (_jack,-1);

	if (_session) {
		_session->reset_jack_connection (_priv_jack);
                jack_bufsize_callback (jack_get_buffer_size (_priv_jack));
		_session->set_frame_rate (jack_get_sample_rate (_priv_jack));
	}

	last_monitor_check = 0;

	jack_on_shutdown (_priv_jack, halted, this);
	jack_set_graph_order_callback (_priv_jack, _graph_order_callback, this);
	jack_set_thread_init_callback (_priv_jack, _thread_init_callback, this);
	// jack_set_process_callback (_priv_jack, _process_callback, this);
	jack_set_process_thread (_priv_jack, _process_thread, this);
	jack_set_sample_rate_callback (_priv_jack, _sample_rate_callback, this);
	jack_set_buffer_size_callback (_priv_jack, _bufsize_callback, this);
	jack_set_xrun_callback (_priv_jack, _xrun_callback, this);
#ifdef HAVE_JACK_SESSION
	if( jack_set_session_callback )
	    jack_set_session_callback (_priv_jack, _session_callback, this);
#endif
	jack_set_sync_callback (_priv_jack, _jack_sync_callback, this);
	jack_set_freewheel_callback (_priv_jack, _freewheel_callback, this);

	if (_session && _session->config.get_jack_time_master()) {
		jack_set_timebase_callback (_priv_jack, 0, _jack_timebase_callback, this);
	}

	if (jack_activate (_priv_jack) == 0) {
		_running = true;
		_has_run = true;
	} else {
		return -1;
	}

	/* re-establish connections */

	for (i = p->begin(); i != p->end(); ++i) {
		(*i)->reconnect ();
	}

	Running (); /* EMIT SIGNAL*/

	start_metering_thread ();

	return 0;
}

int
AudioEngine::request_buffer_size (nframes_t nframes)
{
	GET_PRIVATE_JACK_POINTER_RET (_jack, -1);

	if (nframes == jack_get_buffer_size (_priv_jack)) {
	  return 0;
	}
	
	return jack_set_buffer_size (_priv_jack, nframes);
}

void
AudioEngine::update_total_latencies ()
{
#ifdef HAVE_JACK_RECOMPUTE_LATENCIES
	GET_PRIVATE_JACK_POINTER (_jack);
	jack_recompute_total_latencies (_priv_jack);
#endif
}

string
AudioEngine::make_port_name_relative (string portname)
{
	string::size_type len;
	string::size_type n;

	len = portname.length();

	for (n = 0; n < len; ++n) {
		if (portname[n] == ':') {
			break;
		}
	}

	if ((n != len) && (portname.substr (0, n) == jack_client_name)) {
		return portname.substr (n+1);
	}

	return portname;
}

string
AudioEngine::make_port_name_non_relative (string portname)
{
	string str;

	if (portname.find_first_of (':') != string::npos) {
		return portname;
	}

	str  = jack_client_name;
	str += ':';
	str += portname;

	return str;
}

bool
AudioEngine::is_realtime () const
{
	GET_PRIVATE_JACK_POINTER_RET (_jack,false);
	return jack_is_realtime (_priv_jack);
}

pthread_t
AudioEngine::create_process_thread (boost::function<void()> f, size_t stacksize)
{
        GET_PRIVATE_JACK_POINTER_RET (_jack, 0);
        pthread_t thread;
        ThreadData* td = new ThreadData (this, f, stacksize);

        if (jack_client_create_thread (_priv_jack, &thread, jack_client_real_time_priority (_priv_jack), 
                                       jack_is_realtime (_priv_jack), _start_process_thread, td)) {
                return -1;
        } 

        return thread;
}

void*
AudioEngine::_start_process_thread (void* arg)
{
        ThreadData* td = reinterpret_cast<ThreadData*> (arg);
        boost::function<void()> f = td->f;
        delete td;

        f ();

        return 0;
}
