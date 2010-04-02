/*
    Copyright (C) 2000-2006 Paul Davis

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

#include <fstream>
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <cmath>
#include <cerrno>
#include <string>
#include <climits>
#include <fcntl.h>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/mman.h>


#include "pbd/error.h"
#include "pbd/basename.h"
#include <glibmm/thread.h>
#include "pbd/xml++.h"
#include "pbd/memento_command.h"

#include "ardour/ardour.h"
#include "ardour/audioengine.h"
#include "ardour/debug.h"
#include "ardour/diskstream.h"
#include "ardour/utils.h"
#include "ardour/configuration.h"
#include "ardour/audiofilesource.h"
#include "ardour/send.h"
#include "ardour/playlist.h"
#include "ardour/cycle_timer.h"
#include "ardour/region.h"
#include "ardour/panner.h"
#include "ardour/session.h"
#include "ardour/io.h"
#include "ardour/route.h"

#include "i18n.h"
#include <locale.h>

using namespace std;
using namespace ARDOUR;
using namespace PBD;

/* XXX This goes uninitialized when there is no ~/.config/ardour3 directory.
 * I can't figure out why, so this will do for now (just stole the
 * default from configuration_vars.h).  0 is not a good value for
 * allocating buffer sizes..
 */
ARDOUR::nframes_t Diskstream::disk_io_chunk_frames = 1024 * 256;

PBD::Signal0<void>                Diskstream::DiskOverrun;
PBD::Signal0<void>                Diskstream::DiskUnderrun;

Diskstream::Diskstream (Session &sess, const string &name, Flag flag)
	: SessionObject(sess, name)
        , i_am_the_modifier (0)
        , _route (0)
        , _record_enabled (0)
        , _visible_speed (1.0f)
        , _actual_speed (1.0f)
        , _buffer_reallocation_required (false)
        , _seek_required (false)
        , force_refill (false)
        , capture_start_frame (0)
        , capture_captured (0)
        , was_recording (false)
        , adjust_capture_position (0)
        , _capture_offset (0)
        , _roll_delay (0)
        , first_recordable_frame (max_frames)
        , last_recordable_frame (max_frames)
        , last_possibly_recording (0)
        , _alignment_style (ExistingMaterial)
        , _scrubbing (false)
        , _slaved (false)
        , loop_location (0)
        , overwrite_frame (0)
        , overwrite_offset (0)
        , pending_overwrite (false)
        , overwrite_queued (false)
        , input_change_pending (NoChange)
        , wrap_buffer_size (0)
        , speed_buffer_size (0)
        , _speed (1.0)
        , _target_speed (_speed)
        , file_frame (0)
        , playback_sample (0)
        , playback_distance (0)
        , _read_data_count (0)
        , _write_data_count (0)
        , in_set_state (false)
        , _persistent_alignment_style (ExistingMaterial)
        , first_input_change (true)
        , scrub_start (0)
        , scrub_buffer_size (0)
        , scrub_offset (0)
        , _flags (flag)

{
}

Diskstream::Diskstream (Session& sess, const XMLNode& /*node*/)
	: SessionObject(sess, "unnamed diskstream")
        , i_am_the_modifier (0)
        , _route (0)
        , _record_enabled (0)
        , _visible_speed (1.0f)
        , _actual_speed (1.0f)
        , _buffer_reallocation_required (false)
        , _seek_required (false)
        , force_refill (false)
        , capture_start_frame (0)
        , capture_captured (0)
        , was_recording (false)
        , adjust_capture_position (0)
        , _capture_offset (0)
        , _roll_delay (0)
        , first_recordable_frame (max_frames)
        , last_recordable_frame (max_frames)
        , last_possibly_recording (0)
        , _alignment_style (ExistingMaterial)
        , _scrubbing (false)
        , _slaved (false)
        , loop_location (0)
        , overwrite_frame (0)
        , overwrite_offset (0)
        , pending_overwrite (false)
        , overwrite_queued (false)
        , input_change_pending (NoChange)
        , wrap_buffer_size (0)
        , speed_buffer_size (0)
        , _speed (1.0)
        , _target_speed (_speed)
        , file_frame (0)
        , playback_sample (0)
        , playback_distance (0)
        , _read_data_count (0)
        , _write_data_count (0)
        , in_set_state (false)
        , _persistent_alignment_style (ExistingMaterial)
        , first_input_change (true)
        , scrub_start (0)
        , scrub_buffer_size (0)
        , scrub_offset (0)
        , _flags (Recordable)
{
}

Diskstream::~Diskstream ()
{
	DEBUG_TRACE (DEBUG::Destruction, string_compose ("Diskstream %1 deleted\n", _name));

	if (_playlist) {
		_playlist->release ();
	}
}

void
Diskstream::set_route (Route& r)
{
	_route = &r;
	_io = _route->input();

	ic_connection.disconnect();
	_io->changed.connect_same_thread (ic_connection, boost::bind (&Diskstream::handle_input_change, this, _1, _2));

	input_change_pending = ConfigurationChanged;
	non_realtime_input_change ();
	set_align_style_from_io ();

	_route->Destroyed.connect_same_thread (*this, boost::bind (&Diskstream::route_going_away, this));
}

void
Diskstream::handle_input_change (IOChange change, void * /*src*/)
{
	Glib::Mutex::Lock lm (state_lock);

	if (!(input_change_pending & change)) {
		input_change_pending = IOChange (input_change_pending|change);
		_session.request_input_change_handling ();
	}
}

void
Diskstream::non_realtime_set_speed ()
{
	if (_buffer_reallocation_required)
	{
		Glib::Mutex::Lock lm (state_lock);
		allocate_temporary_buffers ();

		_buffer_reallocation_required = false;
	}

	if (_seek_required) {
		if (speed() != 1.0f || speed() != -1.0f) {
			seek ((nframes_t) (_session.transport_frame() * (double) speed()), true);
		}
		else {
			seek (_session.transport_frame(), true);
		}

		_seek_required = false;
	}
}

bool
Diskstream::realtime_set_speed (double sp, bool global)
{
	bool changed = false;
	double new_speed = sp * _session.transport_speed();

	if (_visible_speed != sp) {
		_visible_speed = sp;
		changed = true;
	}

	if (new_speed != _actual_speed) {

		nframes_t required_wrap_size = (nframes_t) floor (_session.get_block_size() *
									    fabs (new_speed)) + 1;

		if (required_wrap_size > wrap_buffer_size) {
			_buffer_reallocation_required = true;
		}

		_actual_speed = new_speed;
		_target_speed = fabs(_actual_speed);
	}

	if (changed) {
		if (!global) {
			_seek_required = true;
		}
		SpeedChanged (); /* EMIT SIGNAL */
	}

	return _buffer_reallocation_required || _seek_required;
}

void
Diskstream::set_capture_offset ()
{
	if (_io == 0) {
		/* can't capture, so forget it */
		return;
	}

	_capture_offset = _io->latency();
}

void
Diskstream::set_align_style (AlignStyle a)
{
	if (record_enabled() && _session.actively_recording()) {
		return;
	}

	if (a != _alignment_style) {
		_alignment_style = a;
		AlignmentStyleChanged ();
	}
}

int
Diskstream::set_loop (Location *location)
{
	if (location) {
		if (location->start() >= location->end()) {
			error << string_compose(_("Location \"%1\" not valid for track loop (start >= end)"), location->name()) << endl;
			return -1;
		}
	}

	loop_location = location;

	 LoopSet (location); /* EMIT SIGNAL */
	return 0;
}

ARDOUR::nframes_t
Diskstream::get_capture_start_frame (uint32_t n)
{
	Glib::Mutex::Lock lm (capture_info_lock);

	if (capture_info.size() > n) {
		return capture_info[n]->start;
	}
	else {
		return capture_start_frame;
	}
}

ARDOUR::nframes_t
Diskstream::get_captured_frames (uint32_t n)
{
	Glib::Mutex::Lock lm (capture_info_lock);

	if (capture_info.size() > n) {
		return capture_info[n]->frames;
	}
	else {  
		return capture_captured;
	}
}

void
Diskstream::set_roll_delay (ARDOUR::nframes_t nframes)
{
	_roll_delay = nframes;
}

void
Diskstream::set_speed (double sp)
{
	_session.request_diskstream_speed (*this, sp);

	/* to force a rebuffering at the right place */
	playlist_modified();
}

int
Diskstream::use_playlist (boost::shared_ptr<Playlist> playlist)
{
        if (!playlist) {
                return 0;
        }

        bool prior_playlist = false;

	{
		Glib::Mutex::Lock lm (state_lock);

		if (playlist == _playlist) {
			return 0;
		}

		playlist_connections.drop_connections ();

		if (_playlist) {
			_playlist->release();
                        prior_playlist = true;
		}

		_playlist = playlist;
		_playlist->use();

		if (!in_set_state && recordable()) {
			reset_write_sources (false);
		}

		_playlist->ContentsChanged.connect_same_thread (playlist_connections, boost::bind (&Diskstream::playlist_modified, this));
		_playlist->DropReferences.connect_same_thread (playlist_connections, boost::bind (&Diskstream::playlist_deleted, this, boost::weak_ptr<Playlist>(_playlist)));
		_playlist->RangesMoved.connect_same_thread (playlist_connections, boost::bind (&Diskstream::playlist_ranges_moved, this, _1));
	}

	/* don't do this if we've already asked for it *or* if we are setting up
	   the diskstream for the very first time - the input changed handling will
	   take care of the buffer refill.
	*/

	if (!overwrite_queued && prior_playlist) {
		_session.request_overwrite_buffer (this);
		overwrite_queued = true;
	}

	PlaylistChanged (); /* EMIT SIGNAL */
	_session.set_dirty ();

	return 0;
}

void
Diskstream::playlist_changed (const PropertyChange&)
{
	playlist_modified ();
}

void
Diskstream::playlist_modified ()
{
	if (!i_am_the_modifier && !overwrite_queued) {
		_session.request_overwrite_buffer (this);
		overwrite_queued = true;
	}
}

void
Diskstream::playlist_deleted (boost::weak_ptr<Playlist> wpl)
{
	boost::shared_ptr<Playlist> pl (wpl.lock());

	if (pl == _playlist) {

		/* this catches an ordering issue with session destruction. playlists
		   are destroyed before diskstreams. we have to invalidate any handles
		   we have to the playlist.
		*/

		if (_playlist) {
			_playlist.reset ();
		}
	}
}

bool
Diskstream::set_name (const string& str)
{
	if (_name != str) {
		assert(playlist());
		playlist()->set_name (str);

		SessionObject::set_name(str);

		if (!in_set_state && recordable()) {
			/* rename existing capture files so that they have the correct name */
			return rename_write_sources ();
		} else {
			return false;
		}
	}

	return true;
}

void
Diskstream::remove_region_from_last_capture (boost::weak_ptr<Region> wregion)
{
	boost::shared_ptr<Region> region (wregion.lock());

	if (!region) {
		return;
	}

	_last_capture_regions.remove (region);
}

void
Diskstream::playlist_ranges_moved (list< Evoral::RangeMove<framepos_t> > const & movements_frames)
{
	if (!_route || Config->get_automation_follows_regions () == false) {
		return;
	}

	list< Evoral::RangeMove<double> > movements;

	for (list< Evoral::RangeMove<framepos_t> >::const_iterator i = movements_frames.begin();
	     i != movements_frames.end();
	     ++i) {

		movements.push_back(Evoral::RangeMove<double>(i->from, i->length, i->to));
	}

	/* move panner automation */
	boost::shared_ptr<Panner> p = _route->main_outs()->panner ();
	if (p) {
		for (uint32_t i = 0; i < p->npanners (); ++i) {
			boost::shared_ptr<AutomationList> pan_alist = p->streampanner(i).pan_control()->alist();
			XMLNode & before = pan_alist->get_state ();
			pan_alist->move_ranges (movements);
			_session.add_command (new MementoCommand<AutomationList> (
						      *pan_alist.get(), &before, &pan_alist->get_state ()));
		}
	}

	/* move processor automation */
	_route->foreach_processor (boost::bind (&Diskstream::move_processor_automation, this, _1, movements_frames));
}

void
Diskstream::move_processor_automation (boost::weak_ptr<Processor> p, list< Evoral::RangeMove<framepos_t> > const & movements_frames)
{
	boost::shared_ptr<Processor> processor (p.lock ());
	if (!processor) {
		return;
	}

	list< Evoral::RangeMove<double> > movements;
	for (list< Evoral::RangeMove<framepos_t> >::const_iterator i = movements_frames.begin();
		   i != movements_frames.end(); ++i) {
		movements.push_back(Evoral::RangeMove<double>(i->from, i->length, i->to));
	}

	set<Evoral::Parameter> const a = processor->what_can_be_automated ();

	for (set<Evoral::Parameter>::iterator i = a.begin (); i != a.end (); ++i) {
		boost::shared_ptr<AutomationList> al = processor->automation_control(*i)->alist();
		XMLNode & before = al->get_state ();
		al->move_ranges (movements);
		_session.add_command (
			new MementoCommand<AutomationList> (
				*al.get(), &before, &al->get_state ()
				)
			);
	}
}

void
Diskstream::check_record_status (nframes_t transport_frame, nframes_t /*nframes*/, bool can_record)
{
	int possibly_recording;
	int rolling;
	int change;
	const int transport_rolling = 0x4;
	const int track_rec_enabled = 0x2;
	const int global_rec_enabled = 0x1;

	/* merge together the 3 factors that affect record status, and compute
	   what has changed.
	*/

	rolling = _session.transport_speed() != 0.0f;
	possibly_recording = (rolling << 2) | (record_enabled() << 1) | can_record;
	change = possibly_recording ^ last_possibly_recording;

	if (possibly_recording == last_possibly_recording) {
		return;
	}

	/* change state */

	/* if per-track or global rec-enable turned on while the other was already on, we've started recording */

	if (((change & track_rec_enabled) && record_enabled() && (!(change & global_rec_enabled) && can_record)) ||
	    ((change & global_rec_enabled) && can_record && (!(change & track_rec_enabled) && record_enabled()))) {

		/* starting to record: compute first+last frames */

		first_recordable_frame = transport_frame + _capture_offset;
		last_recordable_frame = max_frames;
		capture_start_frame = transport_frame;

		if (!(last_possibly_recording & transport_rolling) && (possibly_recording & transport_rolling)) {

			/* was stopped, now rolling (and recording) */

			if (_alignment_style == ExistingMaterial) {
			  
				first_recordable_frame += _session.worst_output_latency();
				
				DEBUG_TRACE (DEBUG::Latency, string_compose ("Offset rec from stop. Capture offset: %1 Worst O/P Latency: %2 Roll Delay: %3 First Recordable Frame: %4 Transport Frame: %5\n",
									     _capture_offset, _session.worst_output_latency(), _roll_delay, first_recordable_frame, transport_frame));
			} else {
				first_recordable_frame += _roll_delay;
			}

		} else {

			/* was rolling, but record state changed */

			if (_alignment_style == ExistingMaterial) {

				/* manual punch in happens at the correct transport frame
				   because the user hit a button. but to get alignment correct
				   we have to back up the position of the new region to the
				   appropriate spot given the roll delay.
				*/
				
				
				/* autopunch toggles recording at the precise
				   transport frame, and then the DS waits
				   to start recording for a time that depends
				   on the output latency.
				*/
				
				first_recordable_frame += _session.worst_output_latency();
				
				DEBUG_TRACE (DEBUG::Latency, string_compose ("Punch in manual/auto. Capture offset: %1 Worst O/P Latency: %2 Roll Delay: %3 First Recordable Frame: %4 Transport Frame: %5\n",
									     _capture_offset, _session.worst_output_latency(), _roll_delay, first_recordable_frame, transport_frame));
			} else {

				if (_session.config.get_punch_in()) {
					first_recordable_frame += _roll_delay;
				} else {
					capture_start_frame -= _roll_delay;
				}
			}

		}

		prepare_record_status(capture_start_frame);

	} else if (!record_enabled() || !can_record) {

		/* stop recording */

		last_recordable_frame = transport_frame + _capture_offset;

		if (_alignment_style == ExistingMaterial) {
			last_recordable_frame += _session.worst_output_latency();
		} else {
			last_recordable_frame += _roll_delay;
		}
		
		//first_recordable_frame = max_frames;
		
		DEBUG_TRACE (DEBUG::Latency, string_compose ("Stop record - %6 | %7. Capture offset: %1 Worst O/P Latency: %2 Roll Delay: %3 First Recordable Frame: %4 Transport Frame: %5\n",
							     _capture_offset, _session.worst_output_latency(), _roll_delay, first_recordable_frame, transport_frame,
							     can_record, record_enabled()));
	}

	last_possibly_recording = possibly_recording;
}

void
Diskstream::route_going_away ()
{
	_io.reset ();
}

void
Diskstream::calculate_record_range(OverlapType ot, sframes_t transport_frame, nframes_t nframes,
				   nframes_t& rec_nframes, nframes_t& rec_offset)
{
	switch (ot) {
	case OverlapNone:
		rec_nframes = 0;
		break;

	case OverlapInternal:
		/*     ----------    recrange
		         |---|       transrange
		*/
		rec_nframes = nframes;
		rec_offset = 0;
		break;

	case OverlapStart:
		/*    |--------|    recrange
	        -----|          transrange
		*/
		rec_nframes = transport_frame + nframes - first_recordable_frame;
		if (rec_nframes) {
			rec_offset = first_recordable_frame - transport_frame;
		}
		break;

	case OverlapEnd:
		/*    |--------|    recrange
		         |--------  transrange
		*/
		rec_nframes = last_recordable_frame - transport_frame;
		rec_offset = 0;
		break;

	case OverlapExternal:
		/*    |--------|    recrange
		    --------------  transrange
		*/
		rec_nframes = last_recordable_frame - first_recordable_frame;
		rec_offset = first_recordable_frame - transport_frame;
		break;
	}
}
