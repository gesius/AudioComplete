/*
    Copyright (C) 2006 Paul Davis
	By Dave Robillard, 2006

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

#include "pbd/enumwriter.h"
#include "pbd/convert.h"
#include "midi++/events.h"
#include "evoral/midi_util.h"

#include "ardour/amp.h"
#include "ardour/buffer_set.h"
#include "ardour/debug.h"
#include "ardour/delivery.h"
#include "ardour/io_processor.h"
#include "ardour/meter.h"
#include "ardour/midi_diskstream.h"
#include "ardour/midi_playlist.h"
#include "ardour/midi_port.h"
#include "ardour/midi_region.h"
#include "ardour/midi_source.h"
#include "ardour/midi_track.h"
#include "ardour/panner.h"
#include "ardour/port.h"
#include "ardour/processor.h"
#include "ardour/route_group_specialized.h"
#include "ardour/session.h"
#include "ardour/session_playlists.h"
#include "ardour/utils.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

MidiTrack::MidiTrack (Session& sess, string name, Route::Flag flag, TrackMode mode)
	: Track (sess, name, flag, mode, DataType::MIDI)
	, _immediate_events(1024) // FIXME: size?
	, _step_edit_ring_buffer(64) // FIXME: size?
	, _note_mode(Sustained)
	, _step_editing (false)
	, _default_channel (0)
	, _midi_thru (true)
{
}

MidiTrack::~MidiTrack ()
{
}

void
MidiTrack::use_new_diskstream ()
{
	MidiDiskstream::Flag dflags = MidiDiskstream::Flag (0);

	if (_flags & Hidden) {
		dflags = MidiDiskstream::Flag (dflags | MidiDiskstream::Hidden);
	} else {
		dflags = MidiDiskstream::Flag (dflags | MidiDiskstream::Recordable);
	}

	assert(_mode != Destructive);

	boost::shared_ptr<MidiDiskstream> ds (new MidiDiskstream (_session, name(), dflags));
	ds->do_refill_with_alloc ();
	ds->set_block_size (_session.get_block_size ());

	set_diskstream (ds);
}

void
MidiTrack::set_record_enabled (bool yn, void *src)
{
        if (_step_editing) {
                return;
        }

        Track::set_record_enabled (yn, src);
}

void
MidiTrack::set_diskstream (boost::shared_ptr<Diskstream> ds)
{
	Track::set_diskstream (ds);
	
	_diskstream->set_track (this);
	_diskstream->set_destructive (_mode == Destructive);

	_diskstream->set_record_enabled (false);
	//_diskstream->monitor_input (false);

	_diskstream_data_recorded_connection.disconnect ();
	boost::shared_ptr<MidiDiskstream> mds = boost::dynamic_pointer_cast<MidiDiskstream> (ds);
	mds->DataRecorded.connect_same_thread (_diskstream_data_recorded_connection, boost::bind (&MidiTrack::diskstream_data_recorded, this, _1, _2));

	DiskstreamChanged (); /* EMIT SIGNAL */
}

boost::shared_ptr<MidiDiskstream>
MidiTrack::midi_diskstream() const
{
	return boost::dynamic_pointer_cast<MidiDiskstream>(_diskstream);
}

int
MidiTrack::set_state (const XMLNode& node, int version)
{
	return _set_state (node, version, true);
}

int
MidiTrack::_set_state (const XMLNode& node, int version, bool call_base)
{
	const XMLProperty *prop;
	XMLNodeConstIterator iter;

	if (Route::_set_state (node, version, call_base)) {
		return -1;
	}

	// No destructive MIDI tracks (yet?)
	_mode = Normal;

	if ((prop = node.property (X_("note-mode"))) != 0) {
		_note_mode = NoteMode (string_2_enum (prop->value(), _note_mode));
	} else {
		_note_mode = Sustained;
	}

	if ((prop = node.property ("midi-thru")) != 0) {
		set_midi_thru (prop->value() == "yes");
	}

	if ((prop = node.property ("default-channel")) != 0) {
		set_default_channel ((uint8_t) atoi (prop->value()));
	}

	XMLNodeList nlist;
	XMLNodeConstIterator niter;
	XMLNode *child;

	nlist = node.children();

	if (version >= 3000) {
		if ((child = find_named_node (node, X_("Diskstream"))) != 0) {
			boost::shared_ptr<MidiDiskstream> ds (new MidiDiskstream (_session, *child));
			ds->do_refill_with_alloc ();
			set_diskstream (ds);
		}
	}

        /* set rec-enable control *AFTER* setting up diskstream, because it may want to operate
           on the diskstream as it sets its own state
        */

	for (niter = nlist.begin(); niter != nlist.end(); ++niter){
		child = *niter;

                if (child->name() == Controllable::xml_node_name && (prop = child->property ("name")) != 0) {
                        if (prop->value() == X_("recenable")) {
                                _rec_enable_control->set_state (*child, version);
                        }
                }
	}

	pending_state = const_cast<XMLNode*> (&node);

	if (_session.state_of_the_state() & Session::Loading) {
		_session.StateReady.connect_same_thread (*this, boost::bind (&MidiTrack::set_state_part_two, this));
	} else {
		set_state_part_two ();
	}

	return 0;
}

XMLNode&
MidiTrack::state(bool full_state)
{
	XMLNode& root (Route::state(full_state));
	XMLNode* freeze_node;
	char buf[64];

	if (_freeze_record.playlist) {
		XMLNode* inode;

		freeze_node = new XMLNode (X_("freeze-info"));
		freeze_node->add_property ("playlist", _freeze_record.playlist->name());
		freeze_node->add_property ("state", enum_2_string (_freeze_record.state));

		for (vector<FreezeRecordProcessorInfo*>::iterator i = _freeze_record.processor_info.begin(); i != _freeze_record.processor_info.end(); ++i) {
			inode = new XMLNode (X_("processor"));
			(*i)->id.print (buf, sizeof(buf));
			inode->add_property (X_("id"), buf);
			inode->add_child_copy ((*i)->state);

			freeze_node->add_child_nocopy (*inode);
		}

		root.add_child_nocopy (*freeze_node);
	}

	root.add_property (X_("note-mode"), enum_2_string (_note_mode));
	root.add_child_nocopy (_rec_enable_control->get_state());
	root.add_child_nocopy (_diskstream->get_state ());

	root.add_property ("step-editing", (_step_editing ? "yes" : "no"));
	root.add_property ("note-mode", enum_2_string (_note_mode));
	root.add_property ("midi-thru", (_midi_thru ? "yes" : "no"));
	snprintf (buf, sizeof (buf), "%d", (int) _default_channel);
	root.add_property ("default-channel", buf);

	return root;
}

void
MidiTrack::set_state_part_two ()
{
	XMLNode* fnode;
	XMLProperty* prop;
	LocaleGuard lg (X_("POSIX"));

	/* This is called after all session state has been restored but before
	   have been made ports and connections are established.
	*/

	if (pending_state == 0) {
		return;
	}

	if ((fnode = find_named_node (*pending_state, X_("freeze-info"))) != 0) {

		_freeze_record.state = Frozen;

		for (vector<FreezeRecordProcessorInfo*>::iterator i = _freeze_record.processor_info.begin(); i != _freeze_record.processor_info.end(); ++i) {
			delete *i;
		}
		_freeze_record.processor_info.clear ();

		if ((prop = fnode->property (X_("playlist"))) != 0) {
			boost::shared_ptr<Playlist> pl = _session.playlists->by_name (prop->value());
			if (pl) {
				_freeze_record.playlist = boost::dynamic_pointer_cast<MidiPlaylist> (pl);
			} else {
				_freeze_record.playlist.reset();
				_freeze_record.state = NoFreeze;
			return;
			}
		}

		if ((prop = fnode->property (X_("state"))) != 0) {
			_freeze_record.state = FreezeState (string_2_enum (prop->value(), _freeze_record.state));
		}

		XMLNodeConstIterator citer;
		XMLNodeList clist = fnode->children();

		for (citer = clist.begin(); citer != clist.end(); ++citer) {
			if ((*citer)->name() != X_("processor")) {
				continue;
			}

			if ((prop = (*citer)->property (X_("id"))) == 0) {
				continue;
			}

			FreezeRecordProcessorInfo* frii = new FreezeRecordProcessorInfo (*((*citer)->children().front()),
										   boost::shared_ptr<Processor>());
			frii->id = prop->value ();
			_freeze_record.processor_info.push_back (frii);
		}
	}

	if ((fnode = find_named_node (*pending_state, X_("Diskstream"))) != 0) {
		boost::shared_ptr<MidiDiskstream> ds (new MidiDiskstream (_session, *fnode));
		ds->do_refill_with_alloc ();
		ds->set_block_size (_session.get_block_size ());
		set_diskstream (ds);
	}

	return;
}

int
MidiTrack::roll (nframes_t nframes, framepos_t start_frame, framepos_t end_frame, int declick,
		 bool can_record, bool rec_monitors_input, bool& needs_butler)
{
	Glib::RWLock::ReaderLock lm (_processor_lock, Glib::TRY_LOCK);
	if (!lm.locked()) {
		return 0;
	}

	int dret;
	boost::shared_ptr<MidiDiskstream> diskstream = midi_diskstream();

	automation_snapshot (start_frame);

	if (n_outputs().n_total() == 0 && _processors.empty()) {
		return 0;
	}

	if (!_active) {
		silence (nframes);
		return 0;
	}

	nframes_t transport_frame = _session.transport_frame();


	if ((nframes = check_initial_delay (nframes, transport_frame)) == 0) {
		/* need to do this so that the diskstream sets its
		   playback distance to zero, thus causing diskstream::commit
		   to do nothing.
		   */
		return diskstream->process (transport_frame, 0, can_record, rec_monitors_input, needs_butler);
	}

	_silent = false;

	if ((dret = diskstream->process (transport_frame, nframes, can_record, rec_monitors_input, needs_butler)) != 0) {
		silence (nframes);
		return dret;
	}

	/* special condition applies */

	if (_meter_point == MeterInput) {
		_input->process_input (_meter, start_frame, end_frame, nframes);
	}

	if (diskstream->record_enabled() && !can_record && !_session.config.get_auto_input()) {

		/* not actually recording, but we want to hear the input material anyway,
		   at least potentially (depending on monitoring options)
		*/

		passthru (start_frame, end_frame, nframes, 0);

	} else {
		/*
		   XXX is it true that the earlier test on n_outputs()
		   means that we can avoid checking it again here? i think
		   so, because changing the i/o configuration of an IO
		   requires holding the AudioEngine lock, which we hold
		   while in the process() tree.
		   */


		/* copy the diskstream data to all output buffers */

		//const size_t limit = n_process_buffers().n_audio();
		BufferSet& bufs = _session.get_scratch_buffers (n_process_buffers());
		MidiBuffer& mbuf (bufs.get_midi (0));

		/* we are a MIDI track, so we always start the chain with a single-channel diskstream */
		ChanCount c;
		c.set_audio (0);
		c.set_midi (1);
		bufs.set_count (c);

		diskstream->get_playback (mbuf, start_frame, end_frame);

		/* append immediate messages to the first MIDI buffer (thus sending it to the first output port) */

		write_out_of_band_data (bufs, start_frame, end_frame, nframes);

		process_output_buffers (bufs, start_frame, end_frame, nframes,
					(!_session.get_record_enabled() || !Config->get_do_not_record_plugins()), declick);

	}

	_main_outs->flush_buffers (nframes, end_frame - start_frame - 1);

	return 0;
}

int
MidiTrack::no_roll (nframes_t nframes, framepos_t start_frame, framepos_t end_frame,
		    bool state_changing, bool can_record, bool rec_monitors_input)
{
	int ret = Track::no_roll (nframes, start_frame, end_frame, state_changing, can_record, rec_monitors_input);

	if (ret == 0 && _step_editing) {
		push_midi_input_to_step_edit_ringbuffer (nframes);
	}

	return ret;
}

void
MidiTrack::handle_transport_stopped (bool abort, bool did_locate, bool flush_processors)
{
	Route::handle_transport_stopped (abort, did_locate, flush_processors);
}

void
MidiTrack::push_midi_input_to_step_edit_ringbuffer (nframes_t nframes)
{
	PortSet& ports (_input->ports());

	for (PortSet::iterator p = ports.begin(DataType::MIDI); p != ports.end(DataType::MIDI); ++p) {

		Buffer& b (p->get_buffer (nframes));
		const MidiBuffer* const mb = dynamic_cast<MidiBuffer*>(&b);
		assert (mb);

		for (MidiBuffer::const_iterator e = mb->begin(); e != mb->end(); ++e) {

			const Evoral::MIDIEvent<nframes_t> ev(*e, false);

                        /* note on, since for step edit, note length is determined
                           elsewhere 
                        */
                        
                        if (ev.is_note_on()) {
                                /* we don't care about the time for this purpose */
                                _step_edit_ring_buffer.write (0, ev.type(), ev.size(), ev.buffer());
                        }
		}
	}
}

void
MidiTrack::write_out_of_band_data (BufferSet& bufs, framepos_t /*start*/, framepos_t /*end*/, nframes_t nframes)
{
	// Append immediate events
	MidiBuffer& buf (bufs.get_midi (0));
        if (_immediate_events.read_space()) {
                DEBUG_TRACE (DEBUG::MidiIO, string_compose ("%1 has %2 of immediate events to deliver\n", 
                                                            name(), _immediate_events.read_space()));
        }
	_immediate_events.read (buf, 0, 1, nframes-1); // all stamps = 0
        
	// MIDI thru: send incoming data "through" output
	if (_midi_thru && _session.transport_speed() != 0.0f && _input->n_ports().n_midi()) {
		buf.merge_in_place (_input->midi(0)->get_midi_buffer(nframes));
	}
}

int
MidiTrack::export_stuff (BufferSet& /*bufs*/, nframes_t /*nframes*/, framepos_t /*end_frame*/)
{
	return -1;
}

void
MidiTrack::set_latency_delay (nframes_t longest_session_latency)
{
	Route::set_latency_delay (longest_session_latency);
	_diskstream->set_roll_delay (_roll_delay);
}

boost::shared_ptr<Region>
MidiTrack::bounce (InterThreadInfo& /*itt*/)
{
	throw;
	// vector<MidiSource*> srcs;
	// return _session.write_one_track (*this, 0, _session.current_end_frame(), false, srcs, itt);
	return boost::shared_ptr<Region> ();
}


boost::shared_ptr<Region>
MidiTrack::bounce_range (nframes_t /*start*/, nframes_t /*end*/, InterThreadInfo& /*itt*/, bool /*enable_processing*/)
{
	throw;
	//vector<MidiSource*> srcs;
	//return _session.write_one_track (*this, start, end, false, srcs, itt);
	return boost::shared_ptr<Region> ();
}

void
MidiTrack::freeze_me (InterThreadInfo& /*itt*/)
{
}

void
MidiTrack::unfreeze ()
{
	_freeze_record.state = UnFrozen;
	FreezeChange (); /* EMIT SIGNAL */
}

void
MidiTrack::set_note_mode (NoteMode m)
{
	_note_mode = m;
	midi_diskstream()->set_note_mode(m);
}

void
MidiTrack::midi_panic()
{
        DEBUG_TRACE (DEBUG::MidiIO, string_compose ("%1 delivers panic data\n", name()));
	for (uint8_t channel = 0; channel <= 0xF; channel++) {
		uint8_t ev[3] = { MIDI_CMD_CONTROL | channel, MIDI_CTL_SUSTAIN, 0 };
		write_immediate_event(3, ev);
		ev[1] = MIDI_CTL_ALL_NOTES_OFF;
		write_immediate_event(3, ev);
		ev[1] = MIDI_CTL_RESET_CONTROLLERS;
		write_immediate_event(3, ev);
	}
}

/** \return true on success, false on failure (no buffer space left)
 */
bool
MidiTrack::write_immediate_event(size_t size, const uint8_t* buf)
{
	if (!Evoral::midi_event_is_valid(buf, size)) {
		cerr << "WARNING: Ignoring illegal immediate MIDI event" << endl;
		return false;
	}
	const uint32_t type = EventTypeMap::instance().midi_event_type(buf[0]);
	return (_immediate_events.write(0, type, size, buf) == size);
}

void
MidiTrack::MidiControl::set_value(double val)
{
	bool valid = false;
	if (isinf(val)) {
		cerr << "MIDIControl value is infinity" << endl;
	} else if (isnan(val)) {
		cerr << "MIDIControl value is NaN" << endl;
	} else if (val < _list->parameter().min()) {
		cerr << "MIDIControl value is < " << _list->parameter().min() << endl;
	} else if (val > _list->parameter().max()) {
		cerr << "MIDIControl value is > " << _list->parameter().max() << endl;
	} else {
		valid = true;
	}

	if (!valid) {
		return;
	}

	assert(val <= _list->parameter().max());
	if ( ! automation_playback()) {
		size_t size = 3;
		uint8_t ev[3] = { _list->parameter().channel(), int(val), 0 };
		switch(_list->parameter().type()) {
		case MidiCCAutomation:
			ev[0] += MIDI_CMD_CONTROL;
			ev[1] = _list->parameter().id();
			ev[2] = int(val);
			break;

		case MidiPgmChangeAutomation:
			size = 2;
			ev[0] += MIDI_CMD_PGM_CHANGE;
			ev[1] = int(val);
			break;

		case MidiChannelPressureAutomation:
			size = 2;
			ev[0] += MIDI_CMD_CHANNEL_PRESSURE;
			ev[1] = int(val);
			break;

		case MidiPitchBenderAutomation:
			ev[0] += MIDI_CMD_BENDER;
			ev[1] = 0x7F & int(val);
			ev[2] = 0x7F & (int(val) >> 7);
			break;

		default:
			assert(false);
		}
		_route->write_immediate_event(size,  ev);
	}

	AutomationControl::set_value(val);
}

void
MidiTrack::set_step_editing (bool yn)
{
        if (_session.record_status() != Session::Disabled) {
                return;
        }

        if (yn != _step_editing) {
                _step_editing = yn;
                StepEditStatusChange (yn);
        }
}

void
MidiTrack::set_default_channel (uint8_t chn)
{
	_default_channel = std::min ((unsigned int) chn, 15U);
}

void
MidiTrack::set_midi_thru (bool yn)
{
	_midi_thru = yn;
}

boost::shared_ptr<SMFSource>
MidiTrack::write_source (uint32_t)
{
	return midi_diskstream()->write_source ();
}

void
MidiTrack::set_channel_mode (ChannelMode mode, uint16_t mask)
{
	midi_diskstream()->set_channel_mode (mode, mask);
}

ChannelMode
MidiTrack::get_channel_mode ()
{
	return midi_diskstream()->get_channel_mode ();
}

uint16_t
MidiTrack::get_channel_mask ()
{
	return midi_diskstream()->get_channel_mask ();
}

boost::shared_ptr<MidiPlaylist>
MidiTrack::midi_playlist ()
{
	return midi_diskstream()->midi_playlist ();
}

void
MidiTrack::diskstream_data_recorded (boost::shared_ptr<MidiBuffer> buf, boost::weak_ptr<MidiSource> src)
{
	DataRecorded (buf, src); /* EMIT SIGNAL */
}
			       
