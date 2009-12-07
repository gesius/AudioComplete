/*
    Copyright (C) 1999-2002 Paul Davis

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
#include <cerrno>
#include <algorithm>
#include <unistd.h>

#include "pbd/error.h"
#include "pbd/enumwriter.h"

#include <glibmm/thread.h>

#include "ardour/ardour.h"
#include "ardour/audio_diskstream.h"
#include "ardour/audioengine.h"
#include "ardour/auditioner.h"
#include "ardour/butler.h"
#include "ardour/debug.h"
#include "ardour/session.h"
#include "ardour/slave.h"
#include "ardour/timestamps.h"

#include "midi++/manager.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace std;

/** Called by the audio engine when there is work to be done with JACK.
 * @param nframes Number of frames to process.
 */
void
Session::process (nframes_t nframes)
{
	// This is no more the appropriate place to call cycle
	// start. cycle_start needs to be called at the Route::roll()
	// where the signals which we want to mixdown have been calculated.
	//
	MIDI::Manager::instance()->cycle_start(nframes);

	_silent = false;

	if (processing_blocked()) {
		_silent = true;
		return;
	}

	if (non_realtime_work_pending()) {
		if (!_butler->transport_work_requested ()) {
			post_transport ();
		}
	}

	(this->*process_function) (nframes);

	// the ticker is for sending time information like MidiClock
	nframes_t transport_frames = transport_frame();
	BBT_Time  transport_bbt;
	bbt_time(transport_frames, transport_bbt);
	Timecode::Time transport_timecode;
	timecode_time(transport_frames, transport_timecode);
	tick (transport_frames, transport_bbt, transport_timecode); /* EMIT SIGNAL */

	SendFeedback (); /* EMIT SIGNAL */

	MIDI::Manager::instance()->cycle_end();
}

void
Session::prepare_diskstreams ()
{
	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		(*i)->prepare ();
	}
}

int
Session::fail_roll (nframes_t nframes)
{
	return no_roll (nframes);
}

int
Session::no_roll (nframes_t nframes)
{
	nframes_t end_frame = _transport_frame + nframes; // FIXME: varispeed + no_roll ??
	int ret = 0;
	bool declick = get_transport_declick_required();
	boost::shared_ptr<RouteList> r = routes.reader ();

	if (_click_io) {
		_click_io->silence (nframes);
	}

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {

		if ((*i)->is_hidden()) {
			continue;
		}

		(*i)->set_pending_declick (declick);

		if ((*i)->no_roll (nframes, _transport_frame, end_frame, non_realtime_work_pending(),
				   actively_recording(), declick)) {
			error << string_compose(_("Session: error in no roll for %1"), (*i)->name()) << endmsg;
			ret = -1;
			break;
		}
	}

	return ret;
}

int
Session::process_routes (nframes_t nframes)
{
	bool record_active;
	int  declick = get_transport_declick_required();
	bool rec_monitors = get_rec_monitors_input();
	boost::shared_ptr<RouteList> r = routes.reader ();

	if (transport_sub_state & StopPendingCapture) {
		/* force a declick out */
		declick = -1;
	}

	record_active = actively_recording(); // || (get_record_enabled() && get_punch_in());

	const nframes_t start_frame = _transport_frame;
	const nframes_t end_frame = _transport_frame + (nframes_t)floor(nframes * _transport_speed);

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {

		int ret;

		if ((*i)->is_hidden()) {
			continue;
		}

		(*i)->set_pending_declick (declick);

		if ((ret = (*i)->roll (nframes, start_frame, end_frame, declick, record_active, rec_monitors)) < 0) {

			/* we have to do this here. Route::roll() for an AudioTrack will have called AudioDiskstream::process(),
			   and the DS will expect AudioDiskstream::commit() to be called. but we're aborting from that
			   call path, so make sure we release any outstanding locks here before we return failure.
			*/

			boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
			for (DiskstreamList::iterator ids = dsl->begin(); ids != dsl->end(); ++ids) {
				(*ids)->recover ();
			}

			stop_transport ();
			return -1;
		}
	}

	return 0;
}

int
Session::silent_process_routes (nframes_t nframes)
{
	bool record_active = actively_recording();
	int  declick = get_transport_declick_required();
	bool rec_monitors = get_rec_monitors_input();
	boost::shared_ptr<RouteList> r = routes.reader ();

	if (transport_sub_state & StopPendingCapture) {
		/* force a declick out */
		declick = -1;
	}

	const nframes_t start_frame = _transport_frame;
	const nframes_t end_frame = _transport_frame + lrintf(nframes * _transport_speed);

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {

		int ret;

		if ((*i)->is_hidden()) {
			continue;
		}

		if ((ret = (*i)->silent_roll (nframes, start_frame, end_frame, record_active, rec_monitors)) < 0) {

			/* we have to do this here. Route::roll() for an AudioTrack will have called AudioDiskstream::process(),
			   and the DS will expect AudioDiskstream::commit() to be called. but we're aborting from that
			   call path, so make sure we release any outstanding locks here before we return failure.
			*/

			boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
			for (DiskstreamList::iterator ids = dsl->begin(); ids != dsl->end(); ++ids) {
				(*ids)->recover ();
			}

			stop_transport ();
			return -1;
		}
	}

	return 0;
}

void
Session::commit_diskstreams (nframes_t nframes, bool &needs_butler)
{
	int dret;
	float pworst = 1.0f;
	float cworst = 1.0f;

	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {

		if ((*i)->hidden()) {
			continue;
		}

		/* force all diskstreams not handled by a Route to call do their stuff.
		   Note: the diskstreams that were handled by a route will just return zero
		   from this call, because they know they were processed. So in fact, this
		   also runs commit() for every diskstream.
		 */

		if ((dret = (*i)->process (_transport_frame, nframes, actively_recording(), get_rec_monitors_input())) == 0) {
			if ((*i)->commit (nframes)) {
				needs_butler = true;
			}

		} else if (dret < 0) {
			(*i)->recover();
		}

		pworst = min (pworst, (*i)->playback_buffer_load());
		cworst = min (cworst, (*i)->capture_buffer_load());
	}

	uint32_t pmin = g_atomic_int_get (&_playback_load);
	uint32_t pminold = g_atomic_int_get (&_playback_load_min);
	uint32_t cmin = g_atomic_int_get (&_capture_load);
	uint32_t cminold = g_atomic_int_get (&_capture_load_min);

	g_atomic_int_set (&_playback_load, (uint32_t) floor (pworst * 100.0f));
	g_atomic_int_set (&_capture_load, (uint32_t) floor (cworst * 100.0f));
	g_atomic_int_set (&_playback_load_min, min (pmin, pminold));
	g_atomic_int_set (&_capture_load_min, min (cmin, cminold));

	if (actively_recording()) {
		set_dirty();
	}
}

/** Process callback used when the auditioner is not active */
void
Session::process_with_events (nframes_t nframes)
{
	SessionEvent*         ev;
	nframes_t      this_nframes;
	nframes_t      end_frame;
	bool           session_needs_butler = false;
	nframes_t      stop_limit;
	long           frames_moved;

	/* make sure the auditioner is silent */

	if (auditioner) {
		auditioner->silence (nframes);
	}

	/* handle any pending events */

	while (pending_events.read (&ev, 1) == 1) {
		merge_event (ev);
	}

	/* if we are not in the middle of a state change,
	   and there are immediate events queued up,
	   process them.
	*/

	while (!non_realtime_work_pending() && !immediate_events.empty()) {
		SessionEvent *ev = immediate_events.front ();
		immediate_events.pop_front ();
		process_event (ev);
	}

	/* Events caused a transport change, send an MTC Full Frame (Timecode) message.
	 * This is sent whether rolling or not, to give slaves an idea of ardour time
	 * on locates (and allow slow slaves to position and prepare for rolling)
	 */
	if (_send_timecode_update) {
		send_full_time_code(nframes);
		deliver_mmc (MIDI::MachineControl::cmdLocate, _transport_frame);
	}

	if (!process_can_proceed()) {
		_silent = true;
		return;
	}

	if (events.empty() || next_event == events.end()) {
		process_without_events (nframes);
		return;
	}

	if (_transport_speed == 1.0) {
		frames_moved = (long) nframes;
	} else {
		interpolation.set_target_speed (fabs(_target_transport_speed));
		interpolation.set_speed (fabs(_transport_speed));
		frames_moved = (long) interpolation.interpolate (0, nframes, 0, 0);
	}

	end_frame = _transport_frame + (nframes_t)frames_moved;

	{
		SessionEvent* this_event;
		Events::iterator the_next_one;

		if (!process_can_proceed()) {
			_silent = true;
			return;
		}

		if (!_exporting && _slave) {
			if (!follow_slave (nframes)) {
				return;
			}
		}

		if (_transport_speed == 0) {
			no_roll (nframes);
			return;
		}

		if (!_exporting) {
			send_midi_time_code_for_cycle (nframes);
		}

		if (actively_recording()) {
			stop_limit = max_frames;
		} else {

			if (Config->get_stop_at_session_end()) {
				stop_limit = current_end_frame();
			} else {
				stop_limit = max_frames;
			}
		}

		if (maybe_stop (stop_limit)) {
			no_roll (nframes);
			return;
		}

		this_event = *next_event;
		the_next_one = next_event;
		++the_next_one;

		/* yes folks, here it is, the actual loop where we really truly
		   process some audio
		*/

		while (nframes) {

			this_nframes = nframes; /* real (jack) time relative */
			frames_moved = (long) floor (_transport_speed * nframes); /* transport relative */

			/* running an event, position transport precisely to its time */
			if (this_event && this_event->action_frame <= end_frame && this_event->action_frame >= _transport_frame) {
				/* this isn't quite right for reverse play */
				frames_moved = (long) (this_event->action_frame - _transport_frame);
				this_nframes = (nframes_t) abs( floor(frames_moved / _transport_speed) );
			}

			if (this_nframes) {

				click (_transport_frame, this_nframes);

				/* now process frames between now and the first event in this block */
				prepare_diskstreams ();

				if (process_routes (this_nframes)) {
					fail_roll (nframes);
					return;
				}

				commit_diskstreams (this_nframes, session_needs_butler);

				nframes -= this_nframes;

				if (frames_moved < 0) {
					decrement_transport_position (-frames_moved);
				} else {
					increment_transport_position (frames_moved);
				}

				maybe_stop (stop_limit);
				check_declick_out ();
			}

			_engine.split_cycle (this_nframes);

			/* now handle this event and all others scheduled for the same time */

			while (this_event && this_event->action_frame == _transport_frame) {
				process_event (this_event);

				if (the_next_one == events.end()) {
					this_event = 0;
				} else {
					this_event = *the_next_one;
					++the_next_one;
				}
			}

			/* if an event left our state changing, do the right thing */

			if (nframes && non_realtime_work_pending()) {
				no_roll (nframes);
				break;
			}

			/* this is necessary to handle the case of seamless looping */
			end_frame = _transport_frame + (nframes_t) floor (nframes * _transport_speed);

		}

		set_next_event ();

	} /* implicit release of route lock */

	if (session_needs_butler) {
		_butler->summon ();
	}
}

void
Session::reset_slave_state ()
{
	average_slave_delta = 1800;
	delta_accumulator_cnt = 0;
	have_first_delta_accumulator = false;
	_slave_state = Stopped;
}

bool
Session::transport_locked () const
{
	Slave* sl = _slave;

	if (!locate_pending() && (!config.get_external_sync() || (sl && sl->ok() && sl->locked()))) {
		return true;
	}

	return false;
}

bool
Session::follow_slave (nframes_t nframes)
{
	double slave_speed;
	nframes64_t slave_transport_frame;
	nframes_t this_delta;
	int dir;

	if (!_slave->ok()) {
		stop_transport ();
		config.set_external_sync (false);
		goto noroll;
	}

	_slave->speed_and_position (slave_speed, slave_transport_frame);

	DEBUG_TRACE (DEBUG::Slave, string_compose ("Slave position %1 speed %2\n", slave_transport_frame, slave_speed));

	if (!_slave->locked()) {
		DEBUG_TRACE (DEBUG::Slave, "slave not locked\n");
		goto noroll;
	}

	if (slave_transport_frame > _transport_frame) {
		this_delta = slave_transport_frame - _transport_frame;
		dir = 1;
	} else {
		this_delta = _transport_frame - slave_transport_frame;
		dir = -1;
	}

	if (_slave->starting()) {
		slave_speed = 0.0f;
	}

	if (_slave->is_always_synced() || config.get_timecode_source_is_synced()) {

		/* if the TC source is synced, then we assume that its
		   speed is binary: 0.0 or 1.0
		*/

		if (slave_speed != 0.0f) {
			slave_speed = 1.0f;
		}

	} else {

		/* if we are chasing and the average delta between us and the
		   master gets too big, we want to switch to silent
		   motion. so keep track of that here.
		*/

		if (_slave_state == Running) {
			calculate_moving_average_of_slave_delta(dir, this_delta);
		}
	}

	track_slave_state (slave_speed, slave_transport_frame, this_delta);

	DEBUG_TRACE (DEBUG::Slave, string_compose ("slave state %1 @ %2 speed %3 cur delta %4 avg delta %5\n",
						   _slave_state, slave_transport_frame, slave_speed, this_delta, average_slave_delta));
		     

	if (_slave_state == Running && !_slave->is_always_synced() && !config.get_timecode_source_is_synced()) {

		if (_transport_speed != 0.0f) {

			/*
			   note that average_dir is +1 or -1
			*/

			float delta;

			if (average_slave_delta == 0) {
				delta = this_delta;
				delta *= dir;
			} else {
				delta = average_slave_delta;
				delta *= average_dir;
			}

#ifndef NDEBUG
			if (slave_speed != 0.0) {
				DEBUG_TRACE (DEBUG::Slave, string_compose ("delta = %1 speed = %2 ts = %3 M@%4 S@%5 avgdelta %6\n",
									   (int) (dir * this_delta),
									   slave_speed,
									   _transport_speed,
									   _transport_frame,
									   slave_transport_frame, 
									   average_slave_delta));
			}
#endif
			
			if (_slave->give_slave_full_control_over_transport_speed()) {
				set_transport_speed (slave_speed, false, false);
			} else {
				float adjusted_speed = slave_speed + (1.5 * (delta /  float(_current_frame_rate)));
				request_transport_speed (adjusted_speed);
				DEBUG_TRACE (DEBUG::Slave, string_compose ("adjust using %1 towards %2 ratio %3 current %4 slave @ %5\n",
									   delta, adjusted_speed, adjusted_speed/slave_speed, _transport_speed,
									   slave_speed));
			}
			
			if (abs(average_slave_delta) > _slave->resolution()) {
				cerr << "average slave delta greater than slave resolution (" << _slave->resolution() << "), going to silent motion\n";
				goto silent_motion;
			}
		}
	}


	if (_slave_state == Running && !non_realtime_work_pending()) {
		/* speed is set, we're locked, and good to go */
		return true;
	}

  silent_motion:
	DEBUG_TRACE (DEBUG::Slave, "silent motion\n")
	follow_slave_silently (nframes, slave_speed);

  noroll:
	/* don't move at all */
	DEBUG_TRACE (DEBUG::Slave, "no roll\n")
	no_roll (nframes);
	return false;
}

void
Session::calculate_moving_average_of_slave_delta(int dir, nframes_t this_delta)
{
	if (delta_accumulator_cnt >= delta_accumulator_size) {
		have_first_delta_accumulator = true;
		delta_accumulator_cnt = 0;
	}

	if (delta_accumulator_cnt != 0 || this_delta < _current_frame_rate) {
		delta_accumulator[delta_accumulator_cnt++] = long(dir) * long(this_delta);
	}

	if (have_first_delta_accumulator) {
		average_slave_delta = 0L;
		for (int i = 0; i < delta_accumulator_size; ++i) {
			average_slave_delta += delta_accumulator[i];
		}
		average_slave_delta /= long(delta_accumulator_size);
		if (average_slave_delta < 0L) {
			average_dir = -1;
			average_slave_delta = abs(average_slave_delta);
		} else {
			average_dir = 1;
		}
	}
}

void
Session::track_slave_state (float slave_speed, nframes_t slave_transport_frame, nframes_t this_delta)
{
	if (slave_speed != 0.0f) {

		/* slave is running */

		switch (_slave_state) {
		case Stopped:
			if (_slave->requires_seekahead()) {
				slave_wait_end = slave_transport_frame + _slave->seekahead_distance ();
				DEBUG_TRACE (DEBUG::Slave, string_compose ("slave stopped, but running, requires seekahead to %1\n", slave_wait_end));
				/* we can call locate() here because we are in process context */
				locate (slave_wait_end, false, false);
				_slave_state = Waiting;

			} else {

				_slave_state = Running;

				Location* al = _locations.auto_loop_location();

				if (al && play_loop && (slave_transport_frame < al->start() || slave_transport_frame > al->end())) {
					// cancel looping
					request_play_loop(false);
				}

				if (slave_transport_frame != _transport_frame) {
					locate (slave_transport_frame, false, false);
				}
			}
			break;

		case Waiting:
		default:
			break;
		}

		if (_slave_state == Waiting) {

			DEBUG_TRACE (DEBUG::Slave, string_compose ("slave waiting at %1\n", slave_transport_frame));

			if (slave_transport_frame >= slave_wait_end) {

				DEBUG_TRACE (DEBUG::Slave, string_compose ("slave start at %1 vs %2\n", slave_transport_frame, _transport_frame));

				_slave_state = Running;

				/* now perform a "micro-seek" within the disk buffers to realign ourselves
				   precisely with the master.
				*/


				bool ok = true;
				nframes_t frame_delta = slave_transport_frame - _transport_frame;

				boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

				for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
					if (!(*i)->can_internal_playback_seek (frame_delta)) {
						ok = false;
						break;
					}
				}

				if (ok) {
					for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
						(*i)->internal_playback_seek (frame_delta);
					}
					_transport_frame += frame_delta;

				} else {
					cerr << "cannot micro-seek\n";
					/* XXX what? */
				}

				memset (delta_accumulator, 0, sizeof (long) * delta_accumulator_size);
				average_slave_delta = 0L;
				this_delta = 0;
			}
		}

		if (_slave_state == Running && _transport_speed == 0.0f) {
			DEBUG_TRACE (DEBUG::Slave, "slave starts transport\n");
			start_transport ();
		}

	} else { // slave_speed is 0

		/* slave has stopped */

		if (_transport_speed != 0.0f) {
			DEBUG_TRACE (DEBUG::Slave, string_compose ("slave stops transport: %1 frame %2 tf %3\n", slave_speed, slave_transport_frame, _transport_frame));
			stop_transport();
		}

		if (slave_transport_frame != _transport_frame) {
			DEBUG_TRACE (DEBUG::Slave, string_compose ("slave stopped, move to %1\n", slave_transport_frame));
			force_locate (slave_transport_frame, false);
		}

		_slave_state = Stopped;
	}
}

void
Session::follow_slave_silently (nframes_t nframes, float slave_speed)
{
	if (slave_speed && _transport_speed) {

		/* something isn't right, but we should move with the master
		   for now.
		*/

		bool need_butler;

		prepare_diskstreams ();
		silent_process_routes (nframes);
		commit_diskstreams (nframes, need_butler);

		if (need_butler) {
			_butler->summon ();
		}

		int32_t frames_moved = (int32_t) floor (_transport_speed * nframes);

		if (frames_moved < 0) {
			decrement_transport_position (-frames_moved);
		} else {
			increment_transport_position (frames_moved);
		}

		nframes_t stop_limit;

		if (actively_recording()) {
			stop_limit = max_frames;
		} else {
			if (Config->get_stop_at_session_end()) {
				stop_limit = current_end_frame();
			} else {
				stop_limit = max_frames;
			}
		}

		maybe_stop (stop_limit);
	}
}

void
Session::process_without_events (nframes_t nframes)
{
	bool session_needs_butler = false;
	nframes_t stop_limit;
	long frames_moved;

	if (!process_can_proceed()) {
		_silent = true;
		return;
	}

	if (!_exporting && _slave) {
		if (!follow_slave (nframes)) {
			return;
		}
	}

	if (_transport_speed == 0) {
		fail_roll (nframes);
		return;
	}

	if (!_exporting) {
		send_midi_time_code_for_cycle (nframes);
	}

	if (actively_recording()) {
		stop_limit = max_frames;
	} else {
		if (Config->get_stop_at_session_end()) {
			stop_limit = current_end_frame();
		} else {
			stop_limit = max_frames;
		}
	}

	if (maybe_stop (stop_limit)) {
		fail_roll (nframes);
		return;
	}

	if (maybe_sync_start (nframes)) {
		return;
	}

	click (_transport_frame, nframes);

	prepare_diskstreams ();

	if (_transport_speed == 1.0) {
		frames_moved = (long) nframes;
	} else {
		interpolation.set_target_speed (fabs(_target_transport_speed));
		interpolation.set_speed (fabs(_transport_speed));
		frames_moved = (long) interpolation.interpolate (0, nframes, 0, 0);
	}

	if (process_routes (nframes)) {
		fail_roll (nframes);
		return;
	}

	commit_diskstreams (nframes, session_needs_butler);

	if (frames_moved < 0) {
		decrement_transport_position (-frames_moved);
	} else {
		increment_transport_position (frames_moved);
	}

	maybe_stop (stop_limit);
	check_declick_out ();

	if (session_needs_butler) {
		_butler->summon ();
	}
}

/** Process callback used when the auditioner is active.
 * @param nframes number of frames to process.
 */
void
Session::process_audition (nframes_t nframes)
{
	SessionEvent* ev;
	boost::shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if (!(*i)->is_hidden()) {
			(*i)->silence (nframes);
		}
	}

	/* run the auditioner, and if it says we need butler service, ask for it */

	if (auditioner->play_audition (nframes) > 0) {
		_butler->summon ();
	}

	/* handle pending events */

	while (pending_events.read (&ev, 1) == 1) {
		merge_event (ev);
	}

	/* if we are not in the middle of a state change,
	   and there are immediate events queued up,
	   process them.
	*/

	while (!non_realtime_work_pending() && !immediate_events.empty()) {
		SessionEvent *ev = immediate_events.front ();
		immediate_events.pop_front ();
		process_event (ev);
	}

	if (!auditioner->active()) {
		/* auditioner no longer active, so go back to the normal process callback */
		process_function = &Session::process_with_events;
	}
}

bool
Session::maybe_sync_start (nframes_t& nframes)
{
	nframes_t sync_offset;

	if (!waiting_for_sync_offset) {
		return false;
	}

	if (_engine.get_sync_offset (sync_offset) && sync_offset < nframes) {

		/* generate silence up to the sync point, then
		   adjust nframes + offset to reflect whatever
		   is left to do.
		*/

		no_roll (sync_offset);
		nframes -= sync_offset;
		Port::increment_port_offset (sync_offset);
		waiting_for_sync_offset = false;

		if (nframes == 0) {
			return true; // done, nothing left to process
		}

	} else {

		/* sync offset point is not within this process()
		   cycle, so just generate silence. and don't bother
		   with any fancy stuff here, just the minimal silence.
		*/

		_silent = true;

		if (Config->get_locate_while_waiting_for_sync()) {
			if (micro_locate (nframes)) {
				/* XXX ERROR !!! XXX */
			}
		}

		return true; // done, nothing left to process
	}

	return false;
}

void
Session::queue_event (SessionEvent* ev)
{
	if (_state_of_the_state & Deletion) {
		return;
	} else if (_state_of_the_state & Loading) {
		merge_event (ev);
	} else {
		pending_events.write (&ev, 1);
	}
}

void
Session::set_next_event ()
{
	if (events.empty()) {
		next_event = events.end();
		return;
	}

	if (next_event == events.end()) {
		next_event = events.begin();
	}

	if ((*next_event)->action_frame > _transport_frame) {
		next_event = events.begin();
	}

	for (; next_event != events.end(); ++next_event) {
		if ((*next_event)->action_frame >= _transport_frame) {
			break;
		}
	}
}

void
Session::cleanup_event (SessionEvent* ev, int status)
{
	switch (ev->type) {
	case SessionEvent::SetRecordEnable:
		delete ev->routes;
		break;
	default:
		break;
	}
}

void
Session::process_event (SessionEvent* ev)
{
	bool remove = true;
	bool del = true;

	/* if we're in the middle of a state change (i.e. waiting
	   for the butler thread to complete the non-realtime
	   part of the change), we'll just have to queue this
	   event for a time when the change is complete.
	*/

	if (non_realtime_work_pending()) {

		/* except locates, which we have the capability to handle */

		if (ev->type != SessionEvent::Locate) {
			immediate_events.insert (immediate_events.end(), ev);
			_remove_event (ev);
			return;
		}
	}

	DEBUG_TRACE (DEBUG::SessionEvents, string_compose ("Processing event: %1 @ %2\n", enum_2_string (ev->type), _transport_frame));

	switch (ev->type) {
	case SessionEvent::SetLoop:
		set_play_loop (ev->yes_or_no);
		break;

	case SessionEvent::AutoLoop:
		if (play_loop) {
			start_locate (ev->target_frame, true, false, Config->get_seamless_loop());
		}
		remove = false;
		del = false;
		break;

	case SessionEvent::Locate:
		if (ev->yes_or_no) {
			// cerr << "forced locate to " << ev->target_frame << endl;
			locate (ev->target_frame, false, true, false);
		} else {
			// cerr << "soft locate to " << ev->target_frame << endl;
			start_locate (ev->target_frame, false, true, false);
		}
		_send_timecode_update = true;
		break;

	case SessionEvent::LocateRoll:
		if (ev->yes_or_no) {
			// cerr << "forced locate to+roll " << ev->target_frame << endl;
			locate (ev->target_frame, true, true, false);
		} else {
			// cerr << "soft locate to+roll " << ev->target_frame << endl;
			start_locate (ev->target_frame, true, true, false);
		}
		_send_timecode_update = true;
		break;

	case SessionEvent::LocateRollLocate:
		// locate is handled by ::request_roll_at_and_return()
		_requested_return_frame = ev->target_frame;
		request_locate (ev->target2_frame, true);
		break;


	case SessionEvent::SetTransportSpeed:
		set_transport_speed (ev->speed, ev->yes_or_no, ev->second_yes_or_no);
		break;

	case SessionEvent::PunchIn:
		// cerr << "PunchIN at " << transport_frame() << endl;
		if (config.get_punch_in() && record_status() == Enabled) {
			enable_record ();
		}
		remove = false;
		del = false;
		break;

	case SessionEvent::PunchOut:
		// cerr << "PunchOUT at " << transport_frame() << endl;
		if (config.get_punch_out()) {
			step_back_from_record ();
		}
		remove = false;
		del = false;
		break;

	case SessionEvent::StopOnce:
		if (!non_realtime_work_pending()) {
			stop_transport (ev->yes_or_no);
			_clear_event_type (SessionEvent::StopOnce);
		}
		remove = false;
		del = false;
		break;

	case SessionEvent::RangeStop:
		if (!non_realtime_work_pending()) {
			stop_transport (ev->yes_or_no);
		}
		remove = false;
		del = false;
		break;

	case SessionEvent::RangeLocate:
		start_locate (ev->target_frame, true, true, false);
		remove = false;
		del = false;
		break;

	case SessionEvent::Overwrite:
		overwrite_some_buffers (static_cast<Diskstream*>(ev->ptr));
		break;

	case SessionEvent::SetDiskstreamSpeed:
		set_diskstream_speed (static_cast<Diskstream*> (ev->ptr), ev->speed);
		break;

	case SessionEvent::SetSyncSource:
		use_sync_source (ev->slave);
		break;

	case SessionEvent::Audition:
		set_audition (ev->region);
		// drop reference to region
		ev->region.reset ();
		break;

	case SessionEvent::InputConfigurationChange:
		add_post_transport_work (PostTransportInputChange);
		_butler->schedule_transport_work ();
		break;

	case SessionEvent::SetPlayAudioRange:
		set_play_range (ev->audio_range, (ev->speed == 1.0f));
		break;

	case SessionEvent::SetRecordEnable:
		do_record_enable_change_all (ev->routes, ev->yes_or_no);
		break;

	case SessionEvent::RealTimeOperation:
		process_rtop (ev);
		del = false; // other side of RT request needs to clean up
		break;

	default:
	  fatal << string_compose(_("Programming error: illegal event type in process_event (%1)"), ev->type) << endmsg;
		/*NOTREACHED*/
		break;
	};

	if (remove) {
		del = del && !_remove_event (ev);
	}

	if (del) {
		delete ev;
	}
}


void
Session::request_real_time_operation (sigc::slot<void> rt_op, sigc::slot<void,SessionEvent*> callback)
{
	SessionEvent* ev = new SessionEvent (SessionEvent::RealTimeOperation, SessionEvent::Add, SessionEvent::Immediate, 0, 0.0);
	ev->rt_slot =   rt_op;
	ev->rt_return = callback;
	queue_event (ev);
}

void
Session::process_rtop (SessionEvent* ev)
{
	ev->rt_slot ();
	ev->rt_return (ev);
}
