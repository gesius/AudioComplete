/*
    Copyright (C) 2000 Paul Davis 

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

#include <cmath>
#include <fstream>
#include <cassert>
#include <algorithm>

#include <sigc++/bind.h>
#include "pbd/xml++.h"
#include "pbd/enumwriter.h"
#include "pbd/stacktrace.h"
#include "pbd/memento_command.h"

#include "evoral/Curve.hpp"

#include "ardour/amp.h"
#include "ardour/audio_port.h"
#include "ardour/audioengine.h"
#include "ardour/buffer.h"
#include "ardour/buffer_set.h"
#include "ardour/configuration.h"
#include "ardour/cycle_timer.h"
#include "ardour/delivery.h"
#include "ardour/dB.h"
#include "ardour/internal_send.h"
#include "ardour/internal_return.h"
#include "ardour/ladspa_plugin.h"
#include "ardour/meter.h"
#include "ardour/mix.h"
#include "ardour/panner.h"
#include "ardour/plugin_insert.h"
#include "ardour/port.h"
#include "ardour/port_insert.h"
#include "ardour/processor.h"
#include "ardour/profile.h"
#include "ardour/route.h"
#include "ardour/route_group.h"
#include "ardour/send.h"
#include "ardour/session.h"
#include "ardour/timestamps.h"
#include "ardour/utils.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

uint32_t Route::order_key_cnt = 0;
sigc::signal<void, string const &> Route::SyncOrderKeys;

Route::Route (Session& sess, string name, Flag flg, DataType default_type)
	: SessionObject (sess, name)
	, AutomatableControls (sess)
	, _flags (flg)
	, _solo_control (new SoloControllable (X_("solo"), *this))
	, _mute_master (new MuteMaster (sess, name))
	, _default_type (default_type)
	  
{
	init ();
	
	/* add standard processors other than amp (added by ::init()) */
	
	_meter.reset (new PeakMeter (_session));
	add_processor (_meter, PreFader);

	if (_flags & ControlOut) {
		/* where we listen to tracks */
		_intreturn.reset (new InternalReturn (_session));
		add_processor (_intreturn, PreFader);
	}
	
	_main_outs.reset (new Delivery (_session, _output, _mute_master, _name, Delivery::Main));
	add_processor (_main_outs, PostFader);

	/* now that we have _meter, its safe to connect to this */

	_meter_connection = Metering::connect (mem_fun (*this, &Route::meter));
}

Route::Route (Session& sess, const XMLNode& node, DataType default_type)
	: SessionObject (sess, "toBeReset")
	, AutomatableControls (sess)
	, _solo_control (new SoloControllable (X_("solo"), *this))
	, _mute_master (new MuteMaster (sess, "toBeReset"))
	, _default_type (default_type)
{
	init ();

	_set_state (node, false);

	/* now that we have _meter, its safe to connect to this */
	
	_meter_connection = Metering::connect (mem_fun (*this, &Route::meter));
}

void
Route::init ()
{
	_solo_level = 0;
	_solo_isolated = false;
	_active = true;
	processor_max_streams.reset();
	_solo_safe = false;
	_recordable = true;
	order_keys[N_("signal")] = order_key_cnt++;
	_silent = false;
	_meter_point = MeterPostFader;
	_initial_delay = 0;
	_roll_delay = 0;
	_have_internal_generator = false;
	_declickable = false;
	_pending_declick = true;
	_remote_control_id = 0;
	_in_configure_processors = false;
	
	_route_group = 0;

	_phase_invert = 0;
	_denormal_protection = false;

	/* add standard controls */

	add_control (_solo_control);
	add_control (_mute_master);
	
	/* input and output objects */

	_input.reset (new IO (_session, _name, IO::Input, _default_type));
	_output.reset (new IO (_session, _name, IO::Output, _default_type));

	_input->changed.connect (mem_fun (this, &Route::input_change_handler));
	_output->changed.connect (mem_fun (this, &Route::output_change_handler));

	/* add amp processor  */

	_amp.reset (new Amp (_session, _mute_master));
	add_processor (_amp, PostFader);
}

Route::~Route ()
{
	Metering::disconnect (_meter_connection);

	clear_processors (PreFader);
	clear_processors (PostFader);
}

void
Route::set_remote_control_id (uint32_t id)
{
	if (id != _remote_control_id) {
		_remote_control_id = id;
		RemoteControlIDChanged ();
	}
}

uint32_t
Route::remote_control_id() const
{
	return _remote_control_id;
}

long
Route::order_key (std::string const & name) const
{
	OrderKeys::const_iterator i = order_keys.find (name);
	if (i == order_keys.end()) {
		return -1;
	}

	return i->second;
}

void
Route::set_order_key (std::string const & name, long n)
{
	order_keys[name] = n;

	if (Config->get_sync_all_route_ordering()) {
		for (OrderKeys::iterator x = order_keys.begin(); x != order_keys.end(); ++x) {
			x->second = n;
		}
	} 

	_session.set_dirty ();
}

void
Route::sync_order_keys (std::string const & base)
{
	if (order_keys.empty()) {
		return;
	}

	OrderKeys::iterator i;
	uint32_t key;

	if ((i = order_keys.find (base)) == order_keys.end()) {
		/* key doesn't exist, use the first existing key (during session initialization) */
		i = order_keys.begin();
		key = i->second;
		++i;
	} else {
		/* key exists - use it and reset all others (actually, itself included) */
		key = i->second;
		i = order_keys.begin();
	}

	for (; i != order_keys.end(); ++i) {
		i->second = key;
	}
}

string
Route::ensure_track_or_route_name(string name, Session &session)
{
	string newname = name;

	while (session.route_by_name (newname) != NULL) {
		newname = bump_name_once (newname);
	}

	return newname;
}


void
Route::inc_gain (gain_t fraction, void *src)
{
	_amp->inc_gain (fraction, src);
}

void
Route::set_gain (gain_t val, void *src)
{
	if (src != 0 && _route_group && src != _route_group && _route_group->active_property (RouteGroup::Gain)) {
		
		if (_route_group->is_relative()) {

			gain_t usable_gain = _amp->gain();
			if (usable_gain < 0.000001f) {
				usable_gain = 0.000001f;
			}
						
			gain_t delta = val;
			if (delta < 0.000001f) {
				delta = 0.000001f;
			}

			delta -= usable_gain;

			if (delta == 0.0f)
				return;

			gain_t factor = delta / usable_gain;

			if (factor > 0.0f) {
				factor = _route_group->get_max_factor(factor);
				if (factor == 0.0f) {
					_amp->gain_control()->Changed(); /* EMIT SIGNAL */
					return;
				}
			} else {
				factor = _route_group->get_min_factor(factor);
				if (factor == 0.0f) {
					_amp->gain_control()->Changed(); /* EMIT SIGNAL */
					return;
				}
			}
					
			_route_group->apply (&Route::inc_gain, factor, _route_group);

		} else {
			
			_route_group->apply (&Route::set_gain, val, _route_group);
		}

		return;
	} 

	if (val == _amp->gain()) {
		return;
	}

	_amp->set_gain (val, src);
}

/** Process this route for one (sub) cycle (process thread)
 *
 * @param bufs Scratch buffers to use for the signal path
 * @param start_frame Initial transport frame 
 * @param end_frame Final transport frame
 * @param nframes Number of frames to output (to ports)
 *
 * Note that (end_frame - start_frame) may not be equal to nframes when the
 * transport speed isn't 1.0 (eg varispeed).
 */
void
Route::process_output_buffers (BufferSet& bufs,
			       sframes_t start_frame, sframes_t end_frame, nframes_t nframes,
			       bool with_processors, int declick)
{
	bool monitor;

	bufs.is_silent (false);

	switch (Config->get_monitoring_model()) {
	case HardwareMonitoring:
	case ExternalMonitoring:
		monitor = !record_enabled() || (_session.config.get_auto_input() && !_session.actively_recording());
		break;
	default:
		monitor = true;
	}

	if (!declick) {
		declick = _pending_declick;
	}

	/* figure out if we're going to use gain automation */
	_amp->setup_gain_automation (start_frame, end_frame, nframes);
	

	/* tell main outs what to do about monitoring */
	_main_outs->no_outs_cuz_we_no_monitor (!monitor);


	/* -------------------------------------------------------------------------------------------
	   GLOBAL DECLICK (for transport changes etc.)
	   ----------------------------------------------------------------------------------------- */

	if (declick > 0) {
		Amp::apply_gain (bufs, nframes, 0.0, 1.0);
	} else if (declick < 0) {
		Amp::apply_gain (bufs, nframes, 1.0, 0.0);
	} 

	_pending_declick = 0;
		
	/* -------------------------------------------------------------------------------------------
	   DENORMAL CONTROL/PHASE INVERT
	   ----------------------------------------------------------------------------------------- */

	if (_phase_invert) {
		
		int chn = 0;

		if (_denormal_protection || Config->get_denormal_protection()) {
			
			for (BufferSet::audio_iterator i = bufs.audio_begin(); i != bufs.audio_end(); ++i, ++chn) {
				Sample* const sp = i->data();

				if (_phase_invert & chn) {
					for (nframes_t nx = 0; nx < nframes; ++nx) {
						sp[nx]  = -sp[nx];
						sp[nx] += 1.0e-27f;
					}
				} else {
					for (nframes_t nx = 0; nx < nframes; ++nx) {
						sp[nx] += 1.0e-27f;
					}
				}
			}

		} else {

			for (BufferSet::audio_iterator i = bufs.audio_begin(); i != bufs.audio_end(); ++i, ++chn) {
				Sample* const sp = i->data();
				
				if (_phase_invert & chn) {
					for (nframes_t nx = 0; nx < nframes; ++nx) {
						sp[nx] = -sp[nx];
					}
				} 
			}
		}

	} else {

		if (_denormal_protection || Config->get_denormal_protection()) {
			
			for (BufferSet::audio_iterator i = bufs.audio_begin(); i != bufs.audio_end(); ++i) {
				Sample* const sp = i->data();
				for (nframes_t nx = 0; nx < nframes; ++nx) {
					sp[nx] += 1.0e-27f;
				}
			}

		} 
	}

	/* -------------------------------------------------------------------------------------------
	   and go ....
	   ----------------------------------------------------------------------------------------- */

	Glib::RWLock::ReaderLock rm (_processor_lock, Glib::TRY_LOCK);

	if (rm.locked()) {
		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			bufs.set_count (ChanCount::max(bufs.count(), (*i)->input_streams()));
			(*i)->run (bufs, start_frame, end_frame, nframes);
			bufs.set_count (ChanCount::max(bufs.count(), (*i)->output_streams()));
		}

		if (!_processors.empty()) {
			bufs.set_count (ChanCount::max (bufs.count(), _processors.back()->output_streams()));
		}
	}
}

ChanCount
Route::n_process_buffers ()
{
	return max (_input->n_ports(), processor_max_streams);
}

void
Route::passthru (sframes_t start_frame, sframes_t end_frame, nframes_t nframes, int declick)
{
	BufferSet& bufs = _session.get_scratch_buffers (n_process_buffers());

	_silent = false;

	assert (bufs.available() >= _input->n_ports());
	
	if (_input->n_ports() == ChanCount::ZERO) {
		silence (nframes);
	}
	
	bufs.set_count (_input->n_ports());

	if (is_control() && _session.listening()) {
		
		/* control/monitor bus ignores input ports when something is
		   feeding the listen "stream". data will "arrive" into the
		   route from the intreturn processor element.
		*/
			
		bufs.silence (nframes, 0);

	} else {
	
		for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
			
			BufferSet::iterator o = bufs.begin(*t);
			PortSet& ports (_input->ports());
			
			for (PortSet::iterator i = ports.begin(*t); i != ports.end(*t); ++i, ++o) {
				o->read_from (i->get_buffer(nframes), nframes);
			}
		}
	}

	process_output_buffers (bufs, start_frame, end_frame, nframes, true, declick);
}

void
Route::passthru_silence (sframes_t start_frame, sframes_t end_frame, nframes_t nframes, int declick)
{
	process_output_buffers (_session.get_silent_buffers (n_process_buffers()), start_frame, end_frame, nframes, true, declick);
}

void
Route::set_listen (bool yn, void* src)
{
	if (_control_outs) { 
		if (yn != _control_outs->active()) {
			if (yn) {
				_control_outs->activate ();
			} else {
				_control_outs->deactivate ();
			}

			listen_changed (src); /* EMIT SIGNAL */
		}
	}
}

bool
Route::listening () const
{
	if (_control_outs) {
		return _control_outs->active ();
	} else {
		return false;
	}
}

void
Route::set_solo (bool yn, void *src)
{
	if (_solo_safe || _solo_isolated) {
		return;
	}

	if (_route_group && src != _route_group && _route_group->active_property (RouteGroup::Solo)) {
		_route_group->apply (&Route::set_solo, yn, _route_group);
		return;
	}

	if (soloed() != yn) {
		mod_solo_level (yn ? 1 : -1);
		solo_changed (src); /* EMIT SIGNAL */
		_solo_control->Changed (); /* EMIT SIGNAL */
	}	
}

void
Route::mod_solo_level (int32_t delta)
{
	if (delta < 0) {
		if (_solo_level >= (uint32_t) delta) {
			_solo_level += delta;
		} else {
			_solo_level = 0;
		}
	} else {
		_solo_level += delta;
	}

	/* tell main outs what the solo situation is
	 */

	_main_outs->set_solo_level (_solo_level);
	_main_outs->set_solo_isolated (_solo_isolated);
}

void
Route::set_solo_isolated (bool yn, void *src)
{
	if (_route_group && src != _route_group && _route_group->active_property (RouteGroup::Solo)) {
		_route_group->apply (&Route::set_solo_isolated, yn, _route_group);
		return;
	}

	if (yn != _solo_isolated) {
		_solo_isolated = yn;

		/* tell main outs what the solo situation is
		 */
		
		_main_outs->set_solo_level (_solo_level);
		_main_outs->set_solo_isolated (_solo_isolated);

		solo_isolated_changed (src);
	}
}

bool
Route::solo_isolated () const 
{
	return _solo_isolated;
}

void
Route::set_mute (bool yn, void *src)
{
	if (_route_group && src != _route_group && _route_group->active_property (RouteGroup::Mute)) {
		_route_group->apply (&Route::set_mute, yn, _route_group);
		return;
	}

	if (muted() != yn) {
		_mute_master->mute (yn);
		mute_changed (src);
	}
}	

bool
Route::muted() const 
{
	return _mute_master->muted ();
}

#if 0
static void
dump_processors(const string& name, const list<boost::shared_ptr<Processor> >& procs)
{
	cerr << name << " {" << endl;
	for (list<boost::shared_ptr<Processor> >::const_iterator p = procs.begin();
			p != procs.end(); ++p) {
		cerr << "\t" << (*p)->name() << " ID = " << (*p)->id() << endl;
	}
	cerr << "}" << endl;
}
#endif

int
Route::add_processor (boost::shared_ptr<Processor> processor, Placement placement, ProcessorStreams* err)
{
	ProcessorList::iterator loc;

	/* XXX this is not thread safe - we don't hold the lock across determining the iter
	   to add before and actually doing the insertion. dammit.
	*/

	if (placement == PreFader) {
		/* generic pre-fader: insert immediately before the amp */
		loc = find(_processors.begin(), _processors.end(), _amp);
	} else {
		/* generic post-fader: insert at end */
		loc = _processors.end();

		if (processor->visible() && !_processors.empty()) {
			/* check for invisible processors stacked at the end and leave them there */
			ProcessorList::iterator p;
			p = _processors.end();
			--p;
			while (!(*p)->visible() && p != _processors.begin()) {
				--p;
			}
			++p;
			loc = p;
		}
	}

	return add_processor (processor, loc, err);
}


/** Add a processor to the route.
 * If @a iter is not NULL, it must point to an iterator in _processors and the new
 * processor will be inserted immediately before this location.  Otherwise,
 * @a position is used.
 */
int
Route::add_processor (boost::shared_ptr<Processor> processor, ProcessorList::iterator iter, ProcessorStreams* err)
{
	ChanCount old_pms = processor_max_streams;

	if (!_session.engine().connected() || !processor) {
		return 1;
	}

	{
		Glib::RWLock::WriterLock lm (_processor_lock);

		boost::shared_ptr<PluginInsert> pi;
		boost::shared_ptr<PortInsert> porti;

		ProcessorList::iterator loc = find(_processors.begin(), _processors.end(), processor);

		if (processor == _amp || processor == _meter || processor == _main_outs) {
			// Ensure only one of these are in the list at any time
			if (loc != _processors.end()) {
				if (iter == loc) { // Already in place, do nothing
					return 0;
				} else { // New position given, relocate
					_processors.erase (loc);
				}
			}

		} else {
			if (loc != _processors.end()) {
				cerr << "ERROR: Processor added to route twice!" << endl;
				return 1;
			}

			loc = iter;
		}

		_processors.insert (loc, processor);

		// Set up processor list channels.  This will set processor->[input|output]_streams(),
		// configure redirect ports properly, etc.
		

		if (configure_processors_unlocked (err)) {
			ProcessorList::iterator ploc = loc;
			--ploc;
			_processors.erase(ploc);
			configure_processors_unlocked (0); // it worked before we tried to add it ...
			cerr << "configure failed\n";
			return -1;
		}
	
		if ((pi = boost::dynamic_pointer_cast<PluginInsert>(processor)) != 0) {
			
			if (pi->natural_input_streams() == ChanCount::ZERO) {
				/* generator plugin */
				_have_internal_generator = true;
			}
			
		}
		
		// XXX: do we want to emit the signal here ? change call order.
		if (!boost::dynamic_pointer_cast<InternalSend>(processor)) {
			processor->activate ();
		}
		processor->ActiveChanged.connect (bind (mem_fun (_session, &Session::update_latency_compensation), false, false));

		_output->set_user_latency (0);
	}
	
	processors_changed (); /* EMIT SIGNAL */
	
	return 0;
}

bool
Route::add_processor_from_xml (const XMLNode& node, Placement placement)
{
	ProcessorList::iterator loc;

	if (placement == PreFader) {
		/* generic pre-fader: insert immediately before the amp */
		loc = find(_processors.begin(), _processors.end(), _amp);
	} else {
		/* generic post-fader: insert at end */
		loc = _processors.end();
	}

	return add_processor_from_xml (node, loc);
}

bool
Route::add_processor_from_xml (const XMLNode& node, ProcessorList::iterator iter)
{
	const XMLProperty *prop;

	// legacy sessions use a different node name for sends
	if (node.name() == "Send") {
	
		try {
			boost::shared_ptr<Send> send (new Send (_session, _mute_master, node));
			add_processor (send, iter); 
			return true;
		} 
		
		catch (failed_constructor &err) {
			error << _("Send construction failed") << endmsg;
			return false;
		}
		
	} else if (node.name() == "Processor") {
		
		try {
			if ((prop = node.property ("type")) != 0) {

				boost::shared_ptr<Processor> processor;

				if (prop->value() == "ladspa" || prop->value() == "Ladspa" || 
				    prop->value() == "lv2" ||
				    prop->value() == "vst" ||
				    prop->value() == "audiounit") {
					
					processor.reset (new PluginInsert(_session, node));
					
				} else if (prop->value() == "port") {

					processor.reset (new PortInsert (_session, _mute_master, node));
				
				} else if (prop->value() == "send") {

					processor.reset (new Send (_session, _mute_master, node));

				} else if (prop->value() == "meter") {

					if (_meter) {
						if (_meter->set_state (node)) {
							return false;
						} else {
							return true;
						}
					}

					_meter.reset (new PeakMeter (_session, node));						
					processor = _meter;
				
				} else if (prop->value() == "amp") {

					/* amp always exists */
					
					processor = _amp;
					if (processor->set_state (node)) {
						return false;
					} else {
						/* never any reason to add it */
						return true;
					}
					
				} else if (prop->value() == "listen" || prop->value() == "deliver") {

					/* XXX need to generalize */

				} else if (prop->value() == "intsend") {

					processor.reset (new InternalSend (_session, _mute_master, node));

				} else if (prop->value() == "intreturn") {
					
					if (_intreturn) {
						if (_intreturn->set_state (node)) {
							return false;
						} else {
							return true;
						}
					}
					_intreturn.reset (new InternalReturn (_session, node));
					processor = _intreturn;

				} else if (prop->value() == "main-outs") {
					
					if (_main_outs) {
						if (_main_outs->set_state (node)) {
							return false;
						} else {
							return true;
						}
					}

					_main_outs.reset (new Delivery (_session, _output, _mute_master, node));
					processor = _main_outs;

				} else {
					error << string_compose(_("unknown Processor type \"%1\"; ignored"), prop->value()) << endmsg;
				}
				
				if (iter == _processors.end() && processor->visible() && !_processors.empty()) {
					/* check for invisible processors stacked at the end and leave them there */
					ProcessorList::iterator p;
					p = _processors.end();
					--p;
					while (!(*p)->visible() && p != _processors.begin()) {
						--p;
					}
					++p;
					iter = p;
				}

				return (add_processor (processor, iter) == 0);
				
			} else {
				error << _("Processor XML node has no type property") << endmsg;
			}
		}

		catch (failed_constructor &err) {
			warning << _("processor could not be created. Ignored.") << endmsg;
			return false;
		}
	}
	return false;
}

int
Route::add_processors (const ProcessorList& others, Placement placement, ProcessorStreams* err)
{
	ProcessorList::iterator loc;
	if (placement == PreFader) {
		/* generic pre-fader: insert immediately before the amp */
		loc = find(_processors.begin(), _processors.end(), _amp);
	} else {
		/* generic post-fader: insert at end */
		loc = _processors.end();

		if (!_processors.empty()) {
			/* check for invisible processors stacked at the end and leave them there */
			ProcessorList::iterator p;
			p = _processors.end();
			--p;
			cerr << "Let's check " << (*p)->name() << " vis ? " << (*p)->visible() << endl;
			while (!(*p)->visible() && p != _processors.begin()) {
				--p;
			}
			++p;
			loc = p;
		}
	}

	return add_processors (others, loc, err);
}

int
Route::add_processors (const ProcessorList& others, ProcessorList::iterator iter, ProcessorStreams* err)
{
	/* NOTE: this is intended to be used ONLY when copying
	   processors from another Route. Hence the subtle
	   differences between this and ::add_processor()
	*/

	ChanCount old_pms = processor_max_streams;

	if (!_session.engine().connected()) {
		return 1;
	}

	if (others.empty()) {
		return 0;
	}

	{
		Glib::RWLock::WriterLock lm (_processor_lock);
		ProcessorList::iterator existing_end = _processors.end();
		--existing_end;

		ChanCount potential_max_streams = ChanCount::max (_input->n_ports(), _output->n_ports());

		for (ProcessorList::const_iterator i = others.begin(); i != others.end(); ++i) {
			
			// Ensure meter only appears in the list once
			if (*i == _meter) {
				ProcessorList::iterator m = find(_processors.begin(), _processors.end(), *i);
				if (m != _processors.end()) {
					_processors.erase(m);
				}
			}
			
			boost::shared_ptr<PluginInsert> pi;
			
			if ((pi = boost::dynamic_pointer_cast<PluginInsert>(*i)) != 0) {
				pi->set_count (1);
				
				ChanCount m = max(pi->input_streams(), pi->output_streams());
				if (m > potential_max_streams)
					potential_max_streams = m;
			}

			_processors.insert (iter, *i);
			
			if (configure_processors_unlocked (err)) {
				++existing_end;
				_processors.erase (existing_end, _processors.end());
				configure_processors_unlocked (0); // it worked before we tried to add it ...
				return -1;
			}
			
			(*i)->ActiveChanged.connect (bind (mem_fun (_session, &Session::update_latency_compensation), false, false));
		}

		_output->set_user_latency (0);
	}
	
	processors_changed (); /* EMIT SIGNAL */

	return 0;
}

void
Route::placement_range(Placement p, ProcessorList::iterator& start, ProcessorList::iterator& end)
{
	if (p == PreFader) {
		start = _processors.begin();
		end = find(_processors.begin(), _processors.end(), _amp);
	} else {
		start = find(_processors.begin(), _processors.end(), _amp);
		++start;
		end = _processors.end();
	}
}

/** Turn off all processors with a given placement
 * @param p Placement of processors to disable
 */
void
Route::disable_processors (Placement p)
{
	Glib::RWLock::ReaderLock lm (_processor_lock);
	
	ProcessorList::iterator start, end;
	placement_range(p, start, end);
	
	for (ProcessorList::iterator i = start; i != end; ++i) {
		(*i)->deactivate ();
	}

	_session.set_dirty ();
}

/** Turn off all redirects 
 */
void
Route::disable_processors ()
{
	Glib::RWLock::ReaderLock lm (_processor_lock);
	
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		(*i)->deactivate ();
	}
	
	_session.set_dirty ();
}

/** Turn off all redirects with a given placement
 * @param p Placement of redirects to disable
 */
void
Route::disable_plugins (Placement p)
{
	Glib::RWLock::ReaderLock lm (_processor_lock);
	
	ProcessorList::iterator start, end;
	placement_range(p, start, end);
	
	for (ProcessorList::iterator i = start; i != end; ++i) {
		if (boost::dynamic_pointer_cast<PluginInsert> (*i)) {
			(*i)->deactivate ();
		}
	}
	
	_session.set_dirty ();
}

/** Turn off all plugins
 */
void
Route::disable_plugins ()
{
	Glib::RWLock::ReaderLock lm (_processor_lock);
	
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		if (boost::dynamic_pointer_cast<PluginInsert> (*i)) {
			(*i)->deactivate ();
		}
	}
	
	_session.set_dirty ();
}


void
Route::ab_plugins (bool forward)
{
	Glib::RWLock::ReaderLock lm (_processor_lock);
			
	if (forward) {

		/* forward = turn off all active redirects, and mark them so that the next time
		   we go the other way, we will revert them
		*/

		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			if (!boost::dynamic_pointer_cast<PluginInsert> (*i)) {
				continue;
			}

			if ((*i)->active()) {
				(*i)->deactivate ();
				(*i)->set_next_ab_is_active (true);
			} else {
				(*i)->set_next_ab_is_active (false);
			}
		}

	} else {

		/* backward = if the redirect was marked to go active on the next ab, do so */

		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {

			if (!boost::dynamic_pointer_cast<PluginInsert> (*i)) {
				continue;
			}

			if ((*i)->get_next_ab_is_active()) {
				(*i)->activate ();
			} else {
				(*i)->deactivate ();
			}
		}
	}
	
	_session.set_dirty ();
}
	
	
/** Remove processors with a given placement.
 * @param p Placement of processors to remove.
 */
void
Route::clear_processors (Placement p)
{
	const ChanCount old_pms = processor_max_streams;

	if (!_session.engine().connected()) {
		return;
	}
	
	bool already_deleting = _session.deletion_in_progress();
	if (!already_deleting) {
		_session.set_deletion_in_progress();
	}

	{
		Glib::RWLock::WriterLock lm (_processor_lock);
		ProcessorList new_list;
		ProcessorStreams err;

		ProcessorList::iterator amp_loc = find(_processors.begin(), _processors.end(), _amp);
		if (p == PreFader) {
			// Get rid of PreFader processors
			for (ProcessorList::iterator i = _processors.begin(); i != amp_loc; ++i) {
				(*i)->drop_references ();
			}
			// Keep the rest
			for (ProcessorList::iterator i = amp_loc; i != _processors.end(); ++i) {
				new_list.push_back (*i);
			}
		} else {
			// Keep PreFader processors
			for (ProcessorList::iterator i = _processors.begin(); i != amp_loc; ++i) {
				new_list.push_back (*i);
			}
			new_list.push_back (_amp);
			// Get rid of PostFader processors
			for (ProcessorList::iterator i = amp_loc; i != _processors.end(); ++i) {
				(*i)->drop_references ();
			}
		}

		_processors = new_list;
		configure_processors_unlocked (&err); // this can't fail
	}

	processor_max_streams.reset();
	_have_internal_generator = false;
	processors_changed (); /* EMIT SIGNAL */

	if (!already_deleting) {
		_session.clear_deletion_in_progress();
	}
}

int
Route::remove_processor (boost::shared_ptr<Processor> processor, ProcessorStreams* err)
{
	/* these can never be removed */

	if (processor == _amp || processor == _meter || processor == _main_outs) {
		return 0;
	}

	ChanCount old_pms = processor_max_streams;

	if (!_session.engine().connected()) {
		return 1;
	}

	processor_max_streams.reset();

	{
		Glib::RWLock::WriterLock lm (_processor_lock);
		ProcessorList::iterator i;
		bool removed = false;

		for (i = _processors.begin(); i != _processors.end(); ) {
			if (*i == processor) {

				/* move along, see failure case for configure_processors()
				   where we may need to reconfigure the processor.
				*/

				/* stop redirects that send signals to JACK ports
				   from causing noise as a result of no longer being
				   run.
				*/

				boost::shared_ptr<IOProcessor> iop;

				if ((iop = boost::dynamic_pointer_cast<IOProcessor> (*i)) != 0) {
					if (iop->input()) {
						iop->input()->disconnect (this);
					}
					if (iop->output()) {
						iop->output()->disconnect (this);
					}
				}

				i = _processors.erase (i);
				removed = true;
				break;

			} else {
				++i;
			}

			_output->set_user_latency (0);
		}

		if (!removed) {
			/* what? */
			return 1;
		}

		if (configure_processors_unlocked (err)) {
			/* get back to where we where */
			_processors.insert (i, processor);
			/* we know this will work, because it worked before :) */
			configure_processors_unlocked (0);
			return -1;
		}

		_have_internal_generator = false;

		for (i = _processors.begin(); i != _processors.end(); ++i) {
			boost::shared_ptr<PluginInsert> pi;
			
			if ((pi = boost::dynamic_pointer_cast<PluginInsert>(*i)) != 0) {
				if (pi->is_generator()) {
					_have_internal_generator = true;
					break;
				}
			}
		}
	}

	processor->drop_references ();
	processors_changed (); /* EMIT SIGNAL */

	return 0;
}

int
Route::configure_processors (ProcessorStreams* err)
{
	if (!_in_configure_processors) {
		Glib::RWLock::WriterLock lm (_processor_lock);
		return configure_processors_unlocked (err);
	}
	return 0;
}

/** Configure the input/output configuration of each processor in the processors list.
 * Return 0 on success, otherwise configuration is impossible.
 */
int
Route::configure_processors_unlocked (ProcessorStreams* err)
{
	if (_in_configure_processors) {
	   return 0;
	}

	_in_configure_processors = true;

	// Check each processor in order to see if we can configure as requested
	ChanCount in = _input->n_ports ();
	ChanCount out;
	list< pair<ChanCount,ChanCount> > configuration;
	uint32_t index = 0;
	
	for (ProcessorList::iterator p = _processors.begin(); p != _processors.end(); ++p, ++index) {
		if ((*p)->can_support_io_configuration(in, out)) {
			configuration.push_back(make_pair(in, out));
			in = out;
		} else {
			if (err) {
				err->index = index;
				err->count = in;
			}
			_in_configure_processors = false;
			return -1;
		}
	}
	
	// We can, so configure everything
	list< pair<ChanCount,ChanCount> >::iterator c = configuration.begin();
	for (ProcessorList::iterator p = _processors.begin(); p != _processors.end(); ++p, ++c) {
		(*p)->configure_io(c->first, c->second);
		processor_max_streams = ChanCount::max(processor_max_streams, c->first);
		processor_max_streams = ChanCount::max(processor_max_streams, c->second);
		out = c->second;
	}

	// Ensure route outputs match last processor's outputs
	if (out != _output->n_ports ()) {
		_output->ensure_io (out, false, this);
	}

	_in_configure_processors = false;
	return 0;
}

void
Route::all_processors_flip ()
{
	Glib::RWLock::ReaderLock lm (_processor_lock);

	if (_processors.empty()) {
		return;
	}

	bool first_is_on = _processors.front()->active();
	
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		if (first_is_on) {
			(*i)->deactivate ();
		} else {
			(*i)->activate ();
		}
	}
	
	_session.set_dirty ();
}

/** Set all processors with a given placement to a given active state.
 * @param p Placement of processors to change.
 * @param state New active state for those processors.
 */
void
Route::all_processors_active (Placement p, bool state)
{
	Glib::RWLock::ReaderLock lm (_processor_lock);

	if (_processors.empty()) {
		return;
	}
	ProcessorList::iterator start, end;
	placement_range(p, start, end);

	bool before_amp = true;
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		if ((*i) == _amp) {
			before_amp = false;
			continue;
		}
		if (p == PreFader && before_amp) {
			if (state) {
				(*i)->activate ();
			} else {
				(*i)->deactivate ();
			}
		}
	}
	
	_session.set_dirty ();
}

int
Route::reorder_processors (const ProcessorList& new_order, Placement placement, ProcessorStreams* err)
{
	/* "new_order" is an ordered list of processors to be positioned according to "placement".
	   NOTE: all processors in "new_order" MUST be marked as visible. There maybe additional
	   processors in the current actual processor list that are hidden. Any visible processors
	   in the current list but not in "new_order" will be assumed to be deleted.
	*/

	{
		Glib::RWLock::WriterLock lm (_processor_lock);
		ChanCount old_pms = processor_max_streams;
		ProcessorList::iterator oiter;
		ProcessorList::const_iterator niter;
		ProcessorList as_it_was_before = _processors;
		ProcessorList as_it_will_be;
		ProcessorList::iterator start, end;

		placement_range (placement, start, end);

		oiter = start;
		niter = new_order.begin(); 

		while (niter !=  new_order.end()) {
			
			/* if the next processor in the old list is invisible (i.e. should not be in the new order)
			   then append it to the temp list. 

			   Otherwise, see if the next processor in the old list is in the new list. if not,
			   its been deleted. If its there, append it to the temp list.
			*/

			if (oiter == end) {

				/* no more elements in the old list, so just stick the rest of 
				   the new order onto the temp list.
				*/

				as_it_will_be.insert (as_it_will_be.end(), niter, new_order.end());
				while (niter != new_order.end()) {
					(*niter)->set_placement (placement);
					++niter;
				}
				break;

			} else {
				
				if (!(*oiter)->visible()) {

					as_it_will_be.push_back (*oiter);
					(*oiter)->set_placement (placement);

				} else {

					/* visible processor: check that its in the new order */

					if (find (new_order.begin(), new_order.end(), (*oiter)) == new_order.end()) {
						/* deleted: do nothing, shared_ptr<> will clean up */
					} else {
						/* ignore this one, and add the next item from the new order instead */
						as_it_will_be.push_back (*niter);
						(*niter)->set_placement (placement);
						++niter;
					}
				}
				
                                /* now remove from old order - its taken care of no matter what */
				oiter = _processors.erase (oiter);
			}
			
		}

		_processors.insert (oiter, as_it_will_be.begin(), as_it_will_be.end());

		if (configure_processors_unlocked (err)) {
			_processors = as_it_was_before;
			processor_max_streams = old_pms;
			return -1;
		} 
	} 

	processors_changed (); /* EMIT SIGNAL */

	return 0;
}

XMLNode&
Route::get_state()
{
	return state(true);
}

XMLNode&
Route::get_template()
{
	return state(false);
}

XMLNode&
Route::state(bool full_state)
{
	XMLNode *node = new XMLNode("Route");
	ProcessorList::iterator i;
	char buf[32];

	id().print (buf, sizeof (buf));
	node->add_property("id", buf);
	node->add_property ("name", _name);
	node->add_property("default-type", _default_type.to_string());

	if (_flags) {
		node->add_property("flags", enum_2_string (_flags));
	}

	node->add_property("active", _active?"yes":"no");
	node->add_property("phase-invert", _phase_invert?"yes":"no");
	node->add_property("denormal-protection", _denormal_protection?"yes":"no");
	node->add_property("meter-point", enum_2_string (_meter_point));

	if (_route_group) {
		node->add_property("route-group", _route_group->name());
	}

	string order_string;
	OrderKeys::iterator x = order_keys.begin(); 

	while (x != order_keys.end()) {
		order_string += string ((*x).first);
		order_string += '=';
		snprintf (buf, sizeof(buf), "%ld", (*x).second);
		order_string += buf;
		
		++x;

		if (x == order_keys.end()) {
			break;
		}

		order_string += ':';
	}
	node->add_property ("order-keys", order_string);

	node->add_child_nocopy (_input->state (full_state));
	node->add_child_nocopy (_output->state (full_state));
	node->add_child_nocopy (_solo_control->get_state ());
	node->add_child_nocopy (_mute_master->get_state ());

	XMLNode* remote_control_node = new XMLNode (X_("RemoteControl"));
	snprintf (buf, sizeof (buf), "%d", _remote_control_id);
	remote_control_node->add_property (X_("id"), buf);
	node->add_child_nocopy (*remote_control_node);

	if (_comment.length()) {
		XMLNode *cmt = node->add_child ("Comment");
		cmt->add_content (_comment);
	}

	for (i = _processors.begin(); i != _processors.end(); ++i) {
		node->add_child_nocopy((*i)->state (full_state));
	}

	if (_extra_xml){
		node->add_child_copy (*_extra_xml);
	}
	
	return *node;
}

int
Route::set_state (const XMLNode& node)
{
	return _set_state (node, true);
}

int
Route::_set_state (const XMLNode& node, bool call_base)
{

	XMLNodeList nlist;
	XMLNodeConstIterator niter;
	XMLNode *child;
	XMLPropertyList plist;
	const XMLProperty *prop;

	if (node.name() != "Route"){
		error << string_compose(_("Bad node sent to Route::set_state() [%1]"), node.name()) << endmsg;
		return -1;
	}

	if ((prop = node.property (X_("name"))) != 0) {
		Route::set_name (prop->value());
	} 

	if ((prop = node.property ("id")) != 0) {
		_id = prop->value ();
	}

	if ((prop = node.property (X_("flags"))) != 0) {
		_flags = Flag (string_2_enum (prop->value(), _flags));
	} else {
		_flags = Flag (0);
	}

	/* add all processors (except amp, which is always present) */

	nlist = node.children();
	XMLNode processor_state (X_("processor_state"));

	for (niter = nlist.begin(); niter != nlist.end(); ++niter){
		
		child = *niter;

		if (child->name() == IO::state_node_name) {
			if ((prop = child->property (X_("direction"))) == 0) {
				continue;
			}
			
			if (prop->value() == "Input") {
				_input->set_state (*child);
			} else if (prop->value() == "Output") {
				_output->set_state (*child);
			}
		}
			
		if (child->name() == X_("Processor")) {
			processor_state.add_child_copy (*child);
		}
	}

	set_processor_state (processor_state);
	
	if ((prop = node.property ("solo_level")) != 0) {
		_solo_level = 0; // needed for mod_solo_level() to work
		mod_solo_level (atoi (prop->value()));
	}

	if ((prop = node.property ("solo-isolated")) != 0) {
		set_solo_isolated (prop->value() == "yes", this);
	}

	if ((prop = node.property (X_("phase-invert"))) != 0) {
		set_phase_invert (prop->value()=="yes"?true:false);
	}

	if ((prop = node.property (X_("denormal-protection"))) != 0) {
		set_denormal_protection (prop->value()=="yes"?true:false);
	}
	
	if ((prop = node.property (X_("active"))) != 0) {
		bool yn = (prop->value() == "yes");
		_active = !yn; // force switch
		set_active (yn);
	}

	if ((prop = node.property (X_("soloed"))) != 0) {
		bool yn = (prop->value()=="yes");

		/* XXX force reset of solo status */

		set_solo (yn, this);
	}

	if ((prop = node.property (X_("meter-point"))) != 0) {
		_meter_point = MeterPoint (string_2_enum (prop->value (), _meter_point));
	}
	
	if ((prop = node.property (X_("route-group"))) != 0) {
		RouteGroup* route_group = _session.route_group_by_name(prop->value());
		if (route_group == 0) {
			error << string_compose(_("Route %1: unknown route group \"%2 in saved state (ignored)"), _name, prop->value()) << endmsg;
		} else {
			set_route_group (route_group, this);
		}
	}

	if ((prop = node.property (X_("order-keys"))) != 0) {

		long n;

		string::size_type colon, equal;
		string remaining = prop->value();

		while (remaining.length()) {

			if ((equal = remaining.find_first_of ('=')) == string::npos || equal == remaining.length()) {
				error << string_compose (_("badly formed order key string in state file! [%1] ... ignored."), remaining)
				      << endmsg;
			} else {
				if (sscanf (remaining.substr (equal+1).c_str(), "%ld", &n) != 1) {
					error << string_compose (_("badly formed order key string in state file! [%1] ... ignored."), remaining)
					      << endmsg;
				} else {
					set_order_key (remaining.substr (0, equal), n);
				}
			}

			colon = remaining.find_first_of (':');

			if (colon != string::npos) {
				remaining = remaining.substr (colon+1);
			} else {
				break;
			}
		}
	}

	for (niter = nlist.begin(); niter != nlist.end(); ++niter){
		child = *niter;

		if (child->name() == X_("Comment")) {

			/* XXX this is a terrible API design in libxml++ */

			XMLNode *cmt = *(child->children().begin());
			_comment = cmt->content();

		} else if (child->name() == X_("Extra")) {

			_extra_xml = new XMLNode (*child);

		} else if (child->name() == X_("Controllable") && (prop = child->property("name")) != 0) {
			
			if (prop->value() == "solo") {
				_solo_control->set_state (*child);
				_session.add_controllable (_solo_control);
			} 

		} else if (child->name() == X_("RemoteControl")) {
			if ((prop = child->property (X_("id"))) != 0) {
				int32_t x;
				sscanf (prop->value().c_str(), "%d", &x);
				set_remote_control_id (x);
			}

		} else if (child->name() == X_("MuteMaster")) {
			_mute_master->set_state (*child);
		}
	}

	return 0;
}

XMLNode&
Route::get_processor_state ()
{
	XMLNode* root = new XMLNode (X_("redirects"));
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		root->add_child_nocopy ((*i)->state (true));
	}

	return *root;
}

void
Route::set_processor_state (const XMLNode& node)
{
	const XMLNodeList &nlist = node.children();
	XMLNodeConstIterator niter;
	ProcessorList::iterator i, o;

	// Iterate through existing processors, remove those which are not in the state list

	for (i = _processors.begin(); i != _processors.end(); ) {

		/* leave amp alone, always */

		if ((*i) == _amp) {
			++i;
			continue;
		}

		ProcessorList::iterator tmp = i;
		++tmp;

		bool processorInStateList = false;

		for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

			XMLProperty* id_prop = (*niter)->property(X_("id"));

			if (id_prop && (*i)->id() == id_prop->value()) {
				processorInStateList = true;
				break;
			}
		}
		
		if (!processorInStateList) {
			remove_processor (*i);
		}

		i = tmp;
	}

	// Iterate through state list and make sure all processors are on the track and in the correct order,
	// set the state of existing processors according to the new state on the same go

	i = _processors.begin();

	for (niter = nlist.begin(); niter != nlist.end(); ++niter, ++i) {
		
		XMLProperty* prop = (*niter)->property ("type");

		o = i;

		// Check whether the next processor in the list is the right one,
		// except for "amp" which is always there and may not have the
		// old ID since it is always created anew in every Route
		
		if (prop->value() != "amp") {
			while (o != _processors.end()) {
				XMLProperty* id_prop = (*niter)->property(X_("id"));
				if (id_prop && (*o)->id() == id_prop->value()) {
					break;
				}
				
				++o;
			}
		}

		// If the processor (*niter) is not on the route,
		// create it and move it to the correct location

		if (o == _processors.end()) {

			if (add_processor_from_xml (**niter, i)) {
				--i; // move iterator to the newly inserted processor
			} else {
				cerr << "Error restoring route: unable to restore processor" << endl;
			}

		} else {

			// Otherwise, the processor already exists; just
			// ensure it is at the location provided in the XML state

			if (i != o) {
				boost::shared_ptr<Processor> tmp = (*o);
				_processors.erase (o); // remove the old copy
				_processors.insert (i, tmp); // insert the processor at the correct location
				--i; // move iterator to the correct processor
			}

			// and make it (just) so

			(*i)->set_state (**niter);
		}
	}

	/* note: there is no configure_processors() call because we figure that
	   the XML state represents a working signal route.
	*/

	processors_changed ();
}

void
Route::curve_reallocate ()
{
//	_gain_automation_curve.finish_resize ();
//	_pan_automation_curve.finish_resize ();
}

void
Route::silence (nframes_t nframes)
{
	if (!_silent) {

		_output->silence (nframes);
		
		{ 
			Glib::RWLock::ReaderLock lm (_processor_lock, Glib::TRY_LOCK);
			
			if (lm.locked()) {
				for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
					boost::shared_ptr<PluginInsert> pi;

					if (!_active && (pi = boost::dynamic_pointer_cast<PluginInsert> (*i)) != 0) {
						// skip plugins, they don't need anything when we're not active
						continue;
					}
					
					(*i)->silence (nframes);
				}

				if (nframes == _session.get_block_size()) {
					// _silent = true;
				}
			}
		}
		
	}
}	

void
Route::add_internal_return ()
{
	if (!_intreturn) {
		_intreturn.reset (new InternalReturn (_session));
		add_processor (_intreturn, PreFader);
	}
}

BufferSet*
Route::get_return_buffer () const
{
	Glib::RWLock::ReaderLock rm (_processor_lock);
	
	for (ProcessorList::const_iterator x = _processors.begin(); x != _processors.end(); ++x) {
		boost::shared_ptr<InternalReturn> d = boost::dynamic_pointer_cast<InternalReturn>(*x);
		
		if (d) {
			BufferSet* bs = d->get_buffers ();
			return bs;
		}
	}
	
	return 0;
}

void
Route::release_return_buffer () const
{
	Glib::RWLock::ReaderLock rm (_processor_lock);
	
	for (ProcessorList::const_iterator x = _processors.begin(); x != _processors.end(); ++x) {
		boost::shared_ptr<InternalReturn> d = boost::dynamic_pointer_cast<InternalReturn>(*x);
		
		if (d) {
			return d->release_buffers ();
		}
	}
}

int
Route::listen_via (boost::shared_ptr<Route> route, bool active)
{
	vector<string> ports;
	vector<string>::const_iterator i;

	{
		Glib::RWLock::ReaderLock rm (_processor_lock);
		
		for (ProcessorList::iterator x = _processors.begin(); x != _processors.end(); ++x) {

			boost::shared_ptr<InternalSend> d = boost::dynamic_pointer_cast<InternalSend>(*x);

			if (d && d->target_route() == route) {
				
				/* if the target is the control outs, then make sure
				   we take note of which i-send is doing that.
				*/

				if (route == _session.control_out()) {
					_control_outs = boost::dynamic_pointer_cast<Delivery>(d);
				}

				/* already listening via the specified IO: do nothing */
				
				return 0;
			}
		}
	}
	
	boost::shared_ptr<InternalSend> listener;

	try {
		listener.reset (new InternalSend (_session, _mute_master, route));

	} catch (failed_constructor& err) {
		return -1;
	}

	if (route == _session.control_out()) {
		_control_outs = listener;
	}

	add_processor (listener, (Config->get_listen_position() == AfterFaderListen ? PostFader : PreFader));
	
 	return 0;
}	

void
Route::drop_listen (boost::shared_ptr<Route> route)
{
	ProcessorStreams err;
	ProcessorList::iterator tmp;

	Glib::RWLock::ReaderLock rl(_processor_lock);
	rl.acquire ();
	
  again:
	for (ProcessorList::iterator x = _processors.begin(); x != _processors.end(); ) {
		
		boost::shared_ptr<InternalSend> d = boost::dynamic_pointer_cast<InternalSend>(*x);
		
		if (d && d->target_route() == route) {
			rl.release ();
			remove_processor (*x, &err);
			rl.acquire ();

                        /* list could have been demolished while we dropped the lock
			   so start over.
			*/

			goto again; 
		} 
	}

	rl.release ();

	if (route == _session.control_out()) {
		_control_outs.reset ();
	}
}

void
Route::set_route_group (RouteGroup *rg, void *src)
{
	if (rg == _route_group) {
		return;
	}

	if (_route_group) {
		_route_group->remove (this);
	}

	if ((_route_group = rg) != 0) {
		_route_group->add (this);
	}

	_session.set_dirty ();
	route_group_changed (src); /* EMIT SIGNAL */
}

void
Route::drop_route_group (void *src)
{
	_route_group = 0;
	_session.set_dirty ();
	route_group_changed (src); /* EMIT SIGNAL */
}

void
Route::set_comment (string cmt, void *src)
{
	_comment = cmt;
	comment_changed (src);
	_session.set_dirty ();
}

bool
Route::feeds (boost::shared_ptr<Route> other)
{
	// cerr << _name << endl;

	if (_output->connected_to (other->input())) {
		// cerr << "\tdirect FEEDS " << other->name() << endl;
		return true;
	}

	for (ProcessorList::iterator r = _processors.begin(); r != _processors.end(); r++) {
		
		boost::shared_ptr<IOProcessor> iop;
		
		if ((iop = boost::dynamic_pointer_cast<IOProcessor>(*r)) != 0) {
			if (iop->feeds (other)) {
				// cerr << "\tIOP " << iop->name() << " feeds " << other->name() << endl;
				return true;
			} else {
				// cerr << "\tIOP " << iop->name() << " does NOT feeds " << other->name() << endl;
			}
		}
	}

	// cerr << "\tdoes NOT FEED " << other->name() << endl;
	return false;
}

void
Route::handle_transport_stopped (bool abort_ignored, bool did_locate, bool can_flush_processors)
{
	nframes_t now = _session.transport_frame();

	{
		Glib::RWLock::ReaderLock lm (_processor_lock);

		if (!did_locate) {
			automation_snapshot (now, true);
		}

		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			
			if (Config->get_plugins_stop_with_transport() && can_flush_processors) {
				(*i)->deactivate ();
				(*i)->activate ();
			}
			
			(*i)->transport_stopped (now);
		}
	}

	_roll_delay = _initial_delay;
}

void
Route::input_change_handler (IOChange change, void *src)
{
	if ((change & ConfigurationChanged)) {
		configure_processors (0);
	}
}

void
Route::output_change_handler (IOChange change, void *src)
{
	if ((change & ConfigurationChanged)) {
		
		/* XXX resize all listeners to match _main_outs? */
		
		// configure_processors (0);
	}
}

uint32_t
Route::pans_required () const
{
	if (n_outputs().n_audio() < 2) {
		return 0;
	}
	
	return max (n_inputs ().n_audio(), processor_max_streams.n_audio());
}

int 
Route::no_roll (nframes_t nframes, sframes_t start_frame, sframes_t end_frame,  
		bool session_state_changing, bool can_record, bool rec_monitors_input)
{
	if (n_outputs().n_total() == 0) {
		return 0;
	}

	if (session_state_changing || !_active || n_inputs() == ChanCount::ZERO)  {
		silence (nframes);
		return 0;
	}

	_amp->apply_gain_automation (false);
	passthru (start_frame, end_frame, nframes, 0);

	return 0;
}

nframes_t
Route::check_initial_delay (nframes_t nframes, nframes_t& transport_frame)
{
	if (_roll_delay > nframes) {

		_roll_delay -= nframes;
		silence (nframes);
		/* transport frame is not legal for caller to use */
		return 0;

	} else if (_roll_delay > 0) {

		nframes -= _roll_delay;
		silence (_roll_delay);
		/* we've written _roll_delay of samples into the 
		   output ports, so make a note of that for
		   future reference.
		*/
		_main_outs->increment_output_offset (_roll_delay);
		transport_frame += _roll_delay;

		_roll_delay = 0;
	}

	return nframes;
}

int
Route::roll (nframes_t nframes, sframes_t start_frame, sframes_t end_frame, int declick,
	     bool can_record, bool rec_monitors_input)
{
	{
		// automation snapshot can also be called from the non-rt context
		// and it uses the processor list, so we try to acquire the lock here
		Glib::RWLock::ReaderLock lm (_processor_lock, Glib::TRY_LOCK);

		if (lm.locked()) {
			automation_snapshot (_session.transport_frame(), false);
		}
	}

	if (n_outputs().n_total() == 0) {
		return 0;
	}

	if (!_active || n_inputs().n_total() == 0) {
		silence (nframes);
		return 0;
	}
	
	nframes_t unused = 0;

	if ((nframes = check_initial_delay (nframes, unused)) == 0) {
		return 0;
	}

	_silent = false;

	passthru (start_frame, end_frame, nframes, declick);

	return 0;
}

int
Route::silent_roll (nframes_t nframes, sframes_t start_frame, sframes_t end_frame, 
		    bool can_record, bool rec_monitors_input)
{
	silence (nframes);
	return 0;
}

void
Route::toggle_monitor_input ()
{
	for (PortSet::iterator i = _input->ports().begin(); i != _input->ports().end(); ++i) {
		i->ensure_monitor_input( ! i->monitoring_input());
	}
}

bool
Route::has_external_redirects () const
{
	// FIXME: what about sends? - they don't return a signal back to ardour?

	boost::shared_ptr<const PortInsert> pi;
	
	for (ProcessorList::const_iterator i = _processors.begin(); i != _processors.end(); ++i) {

		if ((pi = boost::dynamic_pointer_cast<const PortInsert>(*i)) != 0) {

			for (PortSet::const_iterator port = pi->output()->ports().begin(); port != pi->output()->ports().end(); ++port) {
				
				string port_name = port->name();
				string client_name = port_name.substr (0, port_name.find(':'));

				/* only say "yes" if the redirect is actually in use */
				
				if (client_name != "ardour" && pi->active()) {
					return true;
				}
			}
		}
	}

	return false;
}

void
Route::flush_processors ()
{
	/* XXX shouldn't really try to take this lock, since
	   this is called from the RT audio thread.
	*/

	Glib::RWLock::ReaderLock lm (_processor_lock);

	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		(*i)->deactivate ();
		(*i)->activate ();
	}
}

void
Route::set_meter_point (MeterPoint p, void *src)
{
	if (_meter_point != p) {
		_meter_point = p;

		// Move meter in the processors list
		ProcessorList::iterator loc = find(_processors.begin(), _processors.end(), _meter);
		_processors.erase(loc);
		switch (p) {
		case MeterInput:
			loc = _processors.begin();
			break;
		case MeterPreFader:
			loc = find(_processors.begin(), _processors.end(), _amp);
			break;
		case MeterPostFader:
			loc = _processors.end();
			break;
		}
		_processors.insert(loc, _meter);
		
		 meter_change (src); /* EMIT SIGNAL */
		processors_changed (); /* EMIT SIGNAL */
		_session.set_dirty ();
	}
}
void
Route::put_control_outs_at (Placement p)
{
	if (!_control_outs) {
		return;
	}

	// Move meter in the processors list
	ProcessorList::iterator loc = find(_processors.begin(), _processors.end(), _control_outs);
	_processors.erase(loc);

	switch (p) {
	case PreFader:
		loc = find(_processors.begin(), _processors.end(), _amp);
		if (loc != _processors.begin()) {
			--loc;
		}
		break;
	case PostFader:
		loc = find(_processors.begin(), _processors.end(), _amp);
		assert (loc != _processors.end());
		loc++;
		break;
	}

	_processors.insert(loc, _control_outs);

	processors_changed (); /* EMIT SIGNAL */
	_session.set_dirty ();
}

nframes_t
Route::update_total_latency ()
{
	nframes_t old = _output->effective_latency();
	nframes_t own_latency = _output->user_latency();

	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		if ((*i)->active ()) {
			own_latency += (*i)->signal_latency ();
		}
	}

#undef DEBUG_LATENCY
#ifdef DEBUG_LATENCY
	cerr << _name << ": internal redirect latency = " << own_latency << endl;
#endif

	_output->set_port_latency (own_latency);
	
	if (_output->user_latency() == 0) {

		/* this (virtual) function is used for pure Routes,
		   not derived classes like AudioTrack.  this means
		   that the data processed here comes from an input
		   port, not prerecorded material, and therefore we
		   have to take into account any input latency.
		*/
		
		own_latency += _input->signal_latency ();
	}

	if (old != own_latency) {
		_output->set_latency_delay (own_latency);
		signal_latency_changed (); /* EMIT SIGNAL */
	}
	
#ifdef DEBUG_LATENCY
	cerr << _name << ": input latency = " << _input->signal_latency() << " total = "
	     << own_latency << endl;
#endif

	return _output->effective_latency ();
}

void
Route::set_user_latency (nframes_t nframes)
{
	_output->set_user_latency (nframes);
	_session.update_latency_compensation (false, false);
}

void
Route::set_latency_delay (nframes_t longest_session_latency)
{
	nframes_t old = _initial_delay;

	if (_output->effective_latency() < longest_session_latency) {
		_initial_delay = longest_session_latency - _output->effective_latency();
	} else {
		_initial_delay = 0;
	}

	if (_initial_delay != old) {
		initial_delay_changed (); /* EMIT SIGNAL */
	}

	if (_session.transport_stopped()) {
		_roll_delay = _initial_delay;
	}
}

void
Route::automation_snapshot (nframes_t now, bool force)
{
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		(*i)->automation_snapshot (now, force);
	}
}

Route::SoloControllable::SoloControllable (std::string name, Route& r)
	: AutomationControl (r.session(), Evoral::Parameter (SoloAutomation), 
			     boost::shared_ptr<AutomationList>(), name)
	, route (r)
{
	boost::shared_ptr<AutomationList> gl(new AutomationList(Evoral::Parameter(SoloAutomation)));
	set_list (gl);
}

void
Route::SoloControllable::set_value (float val)
{
	bool bval = ((val >= 0.5f) ? true: false);
	
	route.set_solo (bval, this);
}

float
Route::SoloControllable::get_value (void) const
{
	return route.soloed() ? 1.0f : 0.0f;
}

void 
Route::set_block_size (nframes_t nframes)
{
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		(*i)->set_block_size (nframes);
	}
	_session.ensure_buffers(processor_max_streams);
}

void
Route::protect_automation ()
{
	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i)
		(*i)->protect_automation();
}

void
Route::set_pending_declick (int declick)
{
	if (_declickable) {
		/* this call is not allowed to turn off a pending declick unless "force" is true */
		if (declick) {
			_pending_declick = declick;
		}
		// cerr << _name << ": after setting to " << declick << " pending declick = " << _pending_declick << endl;
	} else {
		_pending_declick = 0;
	}

}

/** Shift automation forwards from a particular place, thereby inserting time.
 *  Adds undo commands for any shifts that are performed.
 *
 * @param pos Position to start shifting from.
 * @param frames Amount to shift forwards by.
 */

void
Route::shift (nframes64_t pos, nframes64_t frames)
{
#ifdef THIS_NEEDS_FIXING_FOR_V3

	/* gain automation */
	XMLNode &before = _gain_control->get_state ();
	_gain_control->shift (pos, frames);
	XMLNode &after = _gain_control->get_state ();
	_session.add_command (new MementoCommand<AutomationList> (_gain_automation_curve, &before, &after));

	/* pan automation */
	for (std::vector<StreamPanner*>::iterator i = _panner->begin (); i != _panner->end (); ++i) {
		Curve & c = (*i)->automation ();
		XMLNode &before = c.get_state ();
		c.shift (pos, frames);
		XMLNode &after = c.get_state ();
		_session.add_command (new MementoCommand<AutomationList> (c, &before, &after));
	}

	/* redirect automation */
	{
		Glib::RWLock::ReaderLock lm (redirect_lock);
		for (RedirectList::iterator i = _redirects.begin (); i != _redirects.end (); ++i) {
			
			set<uint32_t> a;
			(*i)->what_has_automation (a);
			
			for (set<uint32_t>::const_iterator j = a.begin (); j != a.end (); ++j) {
				AutomationList & al = (*i)->automation_list (*j);
				XMLNode &before = al.get_state ();
				al.shift (pos, frames);
				XMLNode &after = al.get_state ();
				_session.add_command (new MementoCommand<AutomationList> (al, &before, &after));
			}
		}
	}
#endif

}


int
Route::save_as_template (const string& path, const string& name)
{
	XMLNode& node (state (false));
	XMLTree tree;
	
	IO::set_name_in_state (*node.children().front(), name);
	
	tree.set_root (&node);
	return tree.write (path.c_str());
}


bool
Route::set_name (const string& str)
{
	bool ret;
	string ioproc_name;
	string name;

	name = Route::ensure_track_or_route_name (str, _session);
	SessionObject::set_name (name);
	
	ret = (_input->set_name(name) && _output->set_name(name));

	if (ret) {
		
		Glib::RWLock::ReaderLock lm (_processor_lock);
		
		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			
			/* rename all I/O processors that have inputs or outputs */

			boost::shared_ptr<IOProcessor> iop = boost::dynamic_pointer_cast<IOProcessor> (*i);

			if (iop && (iop->output() || iop->input())) {
				if (!iop->set_name (name)) {
					ret = false;
				}
			}
		}

	}

	return ret;
}

boost::shared_ptr<Send>
Route::internal_send_for (boost::shared_ptr<const Route> target) const
{
	Glib::RWLock::ReaderLock lm (_processor_lock);

	for (ProcessorList::const_iterator i = _processors.begin(); i != _processors.end(); ++i) {
		boost::shared_ptr<InternalSend> send;
		
		if ((send = boost::dynamic_pointer_cast<InternalSend>(*i)) != 0) {
			if (send->target_route() == target) {
				return send;
			}
		}
	}
	
	return boost::shared_ptr<Send>();
}

void
Route::set_phase_invert (bool yn)
{
	if (_phase_invert != yn) {
		_phase_invert = 0xffff; // XXX all channels
		phase_invert_changed (); /* EMIT SIGNAL */
	}
}

bool
Route::phase_invert () const
{
	return _phase_invert != 0;
}

void
Route::set_denormal_protection (bool yn)
{
	if (_denormal_protection != yn) {
		_denormal_protection = yn;
		denormal_protection_changed (); /* EMIT SIGNAL */
	}
}

bool
Route::denormal_protection () const
{
	return _denormal_protection;
}

void
Route::set_active (bool yn)
{
	if (_active != yn) {
		_active = yn;
		_input->set_active (yn);
		_output->set_active (yn);
		active_changed (); // EMIT SIGNAL
	}
}

void
Route::meter ()
{
	Glib::RWLock::ReaderLock rm (_processor_lock, Glib::TRY_LOCK);
	_meter->meter ();
}

boost::shared_ptr<Panner>
Route::panner() const
{

	return _main_outs->panner();
}

boost::shared_ptr<AutomationControl>
Route::gain_control() const
{

	return _amp->gain_control();
}

boost::shared_ptr<AutomationControl>
Route::get_control (const Evoral::Parameter& param)
{
	/* either we own the control or .... */

	boost::shared_ptr<AutomationControl> c = boost::dynamic_pointer_cast<AutomationControl>(data().control (param));

	if (!c) {

		/* maybe one of our processors does or ... */
		
		Glib::RWLock::ReaderLock rm (_processor_lock, Glib::TRY_LOCK);
		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			if ((c = boost::dynamic_pointer_cast<AutomationControl>((*i)->data().control (param))) != 0) {
				break;
			}
		}
	}
		
	if (!c) {

		/* nobody does so we'll make a new one */

		c = boost::dynamic_pointer_cast<AutomationControl>(control_factory(param));
		add_control(c);
	}

	return c;
}
