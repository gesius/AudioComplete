/*
    Copyright (C) 2006 Paul Davis

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
#include "pbd/error.h"

#include "ardour/amp.h"
#include "ardour/audioplaylist.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/debug.h"
#include "ardour/delivery.h"
#include "ardour/diskstream.h"
#include "ardour/io_processor.h"
#include "ardour/meter.h"
#include "ardour/port.h"
#include "ardour/processor.h"
#include "ardour/route_group_specialized.h"
#include "ardour/session.h"
#include "ardour/track.h"
#include "ardour/utils.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

Track::Track (Session& sess, string name, Route::Flag flag, TrackMode mode, DataType default_type)
	: Route (sess, name, flag, default_type)
        , _saved_meter_point (_meter_point)
        , _mode (mode)
	, _rec_enable_control (new RecEnableControllable(*this))
{
	_freeze_record.state = NoFreeze;
        _declickable = true;
}

Track::~Track ()
{
	DEBUG_TRACE (DEBUG::Destruction, string_compose ("track %1 destructor\n", _name));
}

int
Track::init ()
{
        if (Route::init ()) {
                return -1;
        }

        return 0;
}
XMLNode&
Track::get_state ()
{
	return state (true);
}



XMLNode&
Track::get_template ()
{
	return state (false);
}

void
Track::toggle_monitor_input ()
{
	for (PortSet::iterator i = _input->ports().begin(); i != _input->ports().end(); ++i) {
		i->ensure_monitor_input(!i->monitoring_input());
	}
}

Track::FreezeRecord::~FreezeRecord ()
{
	for (vector<FreezeRecordProcessorInfo*>::iterator i = processor_info.begin(); i != processor_info.end(); ++i) {
		delete *i;
	}
}

Track::FreezeState
Track::freeze_state() const
{
	return _freeze_record.state;
}

Track::RecEnableControllable::RecEnableControllable (Track& s)
	: Controllable (X_("recenable")), track (s)
{
}

void
Track::RecEnableControllable::set_value (double val)
{
	bool bval = ((val >= 0.5) ? true: false);
	track.set_record_enabled (bval, this);
}

double
Track::RecEnableControllable::get_value (void) const
{
	if (track.record_enabled()) { return 1.0; }
	return 0.0;
}

bool
Track::record_enabled () const
{
	return _diskstream && _diskstream->record_enabled ();
}

bool
Track::can_record()
{
	bool will_record = true;
	for (PortSet::iterator i = _input->ports().begin(); i != _input->ports().end() && will_record; ++i) {
		if (!i->connected())
			will_record = false;
	}

	return will_record;
}

void
Track::set_record_enabled (bool yn, void *src)
{
	if (!_session.writable()) {
		return;
	}

	if (_freeze_record.state == Frozen) {
		return;
	}

	if (_route_group && src != _route_group && _route_group->is_active() && _route_group->is_recenable()) {
		_route_group->apply (&Track::set_record_enabled, yn, _route_group);
		return;
	}

	/* keep track of the meter point as it was before we rec-enabled */
	if (!_diskstream->record_enabled()) {
		_saved_meter_point = _meter_point;
	}

	_diskstream->set_record_enabled (yn);

	if (_diskstream->record_enabled()) {
		if (_meter_point != MeterCustom) {
			set_meter_point (MeterInput);
		}
	} else {
		set_meter_point (_saved_meter_point);
	}

	_rec_enable_control->Changed ();
}


bool
Track::set_name (const string& str)
{
	bool ret;

	if (record_enabled() && _session.actively_recording()) {
		/* this messes things up if done while recording */
		return false;
	}

	_diskstream->set_name (str);

	/* save state so that the statefile fully reflects any filename changes */

	if ((ret = Route::set_name (str)) == 0) {
		_session.save_state ("");
	}

	return ret;
}

void
Track::set_latency_compensation (framecnt_t longest_session_latency)
{
	Route::set_latency_compensation (longest_session_latency);
	_diskstream->set_roll_delay (_roll_delay);
}

void
Track::zero_diskstream_id_in_xml (XMLNode& node)
{
	if (node.property ("diskstream-id")) {
		node.add_property ("diskstream-id", "0");
	}
}

int
Track::no_roll (pframes_t nframes, framepos_t start_frame, framepos_t end_frame,
		bool session_state_changing, bool can_record)
{
	Glib::RWLock::ReaderLock lm (_processor_lock, Glib::TRY_LOCK);
	if (!lm.locked()) {
		return 0;
	}

	if (n_outputs().n_total() == 0) {
		return 0;
	}

	if (!_active) {
		silence (nframes);
		return 0;
	}

	if (session_state_changing) {
		if (_session.transport_speed() != 0.0f) {
			/* we're rolling but some state is changing (e.g. our diskstream contents)
			   so we cannot use them. Be silent till this is over. Don't declick.

			   XXX note the absurdity of ::no_roll() being called when we ARE rolling!
			*/
			passthru_silence (start_frame, end_frame, nframes, 0);
			return 0;
		}
		/* we're really not rolling, so we're either delivery silence or actually
		   monitoring, both of which are safe to do while session_state_changing is true.
		*/
	}

	_diskstream->check_record_status (start_frame, can_record);

	bool be_silent;

	if (_have_internal_generator) {
		/* since the instrument has no input streams,
		   there is no reason to send any signal
		   into the route.
		*/
		be_silent = true;
	} else {
                be_silent = send_silence ();
	}

	_amp->apply_gain_automation(false);

	if (be_silent) {

		/* if we're sending silence, but we want the meters to show levels for the signal,
		   meter right here.
		*/

		if (_have_internal_generator) {
			passthru_silence (start_frame, end_frame, nframes, 0);
		} else {
			if (_meter_point == MeterInput) {
				_input->process_input (_meter, start_frame, end_frame, nframes);
			}
			passthru_silence (start_frame, end_frame, nframes, 0);
		}

	} else {

		/* we're sending signal, but we may still want to meter the input.
		 */

		passthru (start_frame, end_frame, nframes, false);
	}

	for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
		boost::shared_ptr<Delivery> d = boost::dynamic_pointer_cast<Delivery> (*i);
		if (d) {
			d->flush_buffers (nframes, end_frame - start_frame - 1);
		}
	}

	return 0;
}

int
Track::silent_roll (pframes_t nframes, framepos_t /*start_frame*/, framepos_t /*end_frame*/,
		    bool can_record, bool& need_butler)
{
	Glib::RWLock::ReaderLock lm (_processor_lock, Glib::TRY_LOCK);
	if (!lm.locked()) {
		return 0;
	}

	if (n_outputs().n_total() == 0 && _processors.empty()) {
		return 0;
	}

	if (!_active) {
		silence (nframes);
		return 0;
	}

	_silent = true;
	_amp->apply_gain_automation(false);

	silence (nframes);

	return _diskstream->process (_session.transport_frame(), nframes, can_record, need_butler);
}

void
Track::set_diskstream (boost::shared_ptr<Diskstream> ds)
{
	_diskstream = ds;

	ds->PlaylistChanged.connect_same_thread (*this, boost::bind (&Track::diskstream_playlist_changed, this));
	diskstream_playlist_changed ();
	ds->RecordEnableChanged.connect_same_thread (*this, boost::bind (&Track::diskstream_record_enable_changed, this));
	ds->SpeedChanged.connect_same_thread (*this, boost::bind (&Track::diskstream_speed_changed, this));
	ds->AlignmentStyleChanged.connect_same_thread (*this, boost::bind (&Track::diskstream_alignment_style_changed, this));
}

void
Track::diskstream_playlist_changed ()
{
	PlaylistChanged (); /* EMIT SIGNAL */
}

void
Track::diskstream_record_enable_changed ()
{
	RecordEnableChanged (); /* EMIT SIGNAL */
}

void
Track::diskstream_speed_changed ()
{
	SpeedChanged (); /* EMIT SIGNAL */
}

void
Track::diskstream_alignment_style_changed ()
{
	AlignmentStyleChanged (); /* EMIT SIGNAL */
}

boost::shared_ptr<Playlist>
Track::playlist ()
{
	return _diskstream->playlist ();
}

void
Track::monitor_input (bool m)
{
	_diskstream->monitor_input (m);
}

bool
Track::destructive () const
{
	return _diskstream->destructive ();
}

list<boost::shared_ptr<Source> > &
Track::last_capture_sources ()
{
	return _diskstream->last_capture_sources ();
}

void
Track::set_capture_offset ()
{
	_diskstream->set_capture_offset ();
}

list<boost::shared_ptr<Source> >
Track::steal_write_sources()
{
        return _diskstream->steal_write_sources ();
}

void
Track::reset_write_sources (bool r, bool force)
{
	_diskstream->reset_write_sources (r, force);
}

float
Track::playback_buffer_load () const
{
	return _diskstream->playback_buffer_load ();
}

float
Track::capture_buffer_load () const
{
	return _diskstream->capture_buffer_load ();
}

int
Track::do_refill ()
{
	return _diskstream->do_refill ();
}

int
Track::do_flush (RunContext c, bool force)
{
	return _diskstream->do_flush (c, force);
}

void
Track::set_pending_overwrite (bool o)
{
	_diskstream->set_pending_overwrite (o);
}

int
Track::seek (framepos_t p, bool complete_refill)
{
	return _diskstream->seek (p, complete_refill);
}

bool
Track::hidden () const
{
	return _diskstream->hidden ();
}

int
Track::can_internal_playback_seek (framepos_t p)
{
	return _diskstream->can_internal_playback_seek (p);
}

int
Track::internal_playback_seek (framepos_t p)
{
	return _diskstream->internal_playback_seek (p);
}

void
Track::non_realtime_input_change ()
{
	_diskstream->non_realtime_input_change ();
}

void
Track::non_realtime_locate (framepos_t p)
{
	_diskstream->non_realtime_locate (p);
}

void
Track::non_realtime_set_speed ()
{
	_diskstream->non_realtime_set_speed ();
}

int
Track::overwrite_existing_buffers ()
{
	return _diskstream->overwrite_existing_buffers ();
}

framecnt_t
Track::get_captured_frames (uint32_t n) const
{
	return _diskstream->get_captured_frames (n);
}

int
Track::set_loop (Location* l)
{
	return _diskstream->set_loop (l);
}

void
Track::transport_looped (framepos_t p)
{
	_diskstream->transport_looped (p);
}

bool
Track::realtime_set_speed (double s, bool g)
{
	return _diskstream->realtime_set_speed (s, g);
}

void
Track::transport_stopped_wallclock (struct tm & n, time_t t, bool g)
{
	_diskstream->transport_stopped_wallclock (n, t, g);
}

bool
Track::pending_overwrite () const
{
	return _diskstream->pending_overwrite ();
}

double
Track::speed () const
{
	return _diskstream->speed ();
}

void
Track::prepare_to_stop (framepos_t p)
{
	_diskstream->prepare_to_stop (p);
}

void
Track::set_slaved (bool s)
{
	_diskstream->set_slaved (s);
}

ChanCount
Track::n_channels ()
{
	return _diskstream->n_channels ();
}

framepos_t
Track::get_capture_start_frame (uint32_t n) const
{
	return _diskstream->get_capture_start_frame (n);
}

AlignStyle
Track::alignment_style () const
{
	return _diskstream->alignment_style ();
}

AlignChoice
Track::alignment_choice () const
{
	return _diskstream->alignment_choice ();
}

framepos_t
Track::current_capture_start () const
{
	return _diskstream->current_capture_start ();
}

framepos_t
Track::current_capture_end () const
{
	return _diskstream->current_capture_end ();
}

void
Track::playlist_modified ()
{
	_diskstream->playlist_modified ();
}

int
Track::use_playlist (boost::shared_ptr<Playlist> p)
{
	return _diskstream->use_playlist (p);
}

int
Track::use_copy_playlist ()
{
	return _diskstream->use_copy_playlist ();
}

int
Track::use_new_playlist ()
{
	return _diskstream->use_new_playlist ();
}

uint32_t
Track::read_data_count () const
{
	return _diskstream->read_data_count ();
}

void
Track::set_align_style (AlignStyle s, bool force)
{
	_diskstream->set_align_style (s, force);
}

void
Track::set_align_choice (AlignChoice s, bool force)
{
	_diskstream->set_align_choice (s, force);
}

uint32_t
Track::write_data_count () const
{
	return _diskstream->write_data_count ();
}

PBD::ID const &
Track::diskstream_id () const
{
	return _diskstream->id ();
}

void
Track::set_block_size (pframes_t n)
{
	Route::set_block_size (n);
	_diskstream->set_block_size (n);
}

void
Track::adjust_playback_buffering ()
{
        if (_diskstream) {
                _diskstream->adjust_playback_buffering ();
        }
}

void
Track::adjust_capture_buffering ()
{
        if (_diskstream) {
                _diskstream->adjust_capture_buffering ();
        }
}

bool
Track::send_silence () const
{
        /*
          ADATs work in a strange way..
          they monitor input always when stopped.and auto-input is engaged.

          Other machines switch to input on stop if the track is record enabled,
          regardless of the auto input setting (auto input only changes the
          monitoring state when the transport is rolling)
        */

        bool send_silence;

        if (!Config->get_tape_machine_mode()) {
                /*
                  ADATs work in a strange way..
                  they monitor input always when stopped.and auto-input is engaged.
                */
                if ((Config->get_monitoring_model() == SoftwareMonitoring)
                    && (_session.config.get_auto_input () || _diskstream->record_enabled())) {
                        send_silence = false;
                } else {
                        send_silence = true;
                }
        } else {
                /*
                  Other machines switch to input on stop if the track is record enabled,
                  regardless of the auto input setting (auto input only changes the
                  monitoring state when the transport is rolling)
                */
                if ((Config->get_monitoring_model() == SoftwareMonitoring)
                    && _diskstream->record_enabled()) {
                        send_silence = false;
                } else {
                        send_silence = true;
                }
        }

        return send_silence;
}

void
Track::maybe_declick (BufferSet& bufs, framecnt_t nframes, int declick)
{
        /* never declick if there is an internal generator - we just want it to
           keep generating sound without interruption.
        */

        if (_have_internal_generator) {
                return;
        }

        if (!declick) {
		declick = _pending_declick;
	}

	if (declick != 0) {
		Amp::declick (bufs, nframes, declick);
	}
}

framecnt_t
Track::check_initial_delay (framecnt_t nframes, framecnt_t& transport_frame)
{
	if (_roll_delay > nframes) {

		_roll_delay -= nframes;
		silence_unlocked (nframes);
		/* transport frame is not legal for caller to use */
		return 0;

	} else if (_roll_delay > 0) {

		nframes -= _roll_delay;
		silence_unlocked (_roll_delay);
		transport_frame += _roll_delay;

		/* shuffle all the port buffers for things that lead "out" of this Route
		   to reflect that we just wrote _roll_delay frames of silence.
		*/

		Glib::RWLock::ReaderLock lm (_processor_lock);
		for (ProcessorList::iterator i = _processors.begin(); i != _processors.end(); ++i) {
			boost::shared_ptr<IOProcessor> iop = boost::dynamic_pointer_cast<IOProcessor> (*i);
			if (iop) {
				iop->increment_port_buffer_offset (_roll_delay);
			}
		}
		_output->increment_port_buffer_offset (_roll_delay);

		_roll_delay = 0;

	}

	return nframes;
}

