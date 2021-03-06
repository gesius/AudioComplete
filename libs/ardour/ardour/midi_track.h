/*
    Copyright (C) 2006 Paul Davis
    Author: David Robillard

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

#ifndef __ardour_midi_track_h__
#define __ardour_midi_track_h__

#include "ardour/midi_channel_filter.h"
#include "ardour/midi_ring_buffer.h"
#include "ardour/track.h"

namespace ARDOUR
{

class InterThreadInfo;
class MidiDiskstream;
class MidiPlaylist;
class RouteGroup;
class SMFSource;
class Session;

class LIBARDOUR_API MidiTrack : public Track
{
public:
	MidiTrack (Session&, std::string name, Route::Flag f = Route::Flag (0), TrackMode m = Normal);
	~MidiTrack ();

	int init ();

	int roll (pframes_t nframes, framepos_t start_frame, framepos_t end_frame, int declick, bool& need_butler);

	void realtime_handle_transport_stopped ();
	void realtime_locate ();
	void non_realtime_locate (framepos_t);

	boost::shared_ptr<Diskstream> create_diskstream ();
	void set_diskstream (boost::shared_ptr<Diskstream>);
	void set_record_enabled (bool yn, PBD::Controllable::GroupControlDisposition);
	void set_record_safe (bool yn, PBD::Controllable::GroupControlDisposition);

	DataType data_type () const {
		return DataType::MIDI;
	}

	void freeze_me (InterThreadInfo&);
	void unfreeze ();

	bool bounceable (boost::shared_ptr<Processor>, bool) const { return false; }
	boost::shared_ptr<Region> bounce (InterThreadInfo&);
	boost::shared_ptr<Region> bounce_range (framepos_t                   start,
	                                        framepos_t                   end,
	                                        InterThreadInfo&             iti,
	                                        boost::shared_ptr<Processor> endpoint,
	                                        bool                         include_endpoint);

	int export_stuff (BufferSet&                   bufs,
	                  framepos_t                   start_frame,
	                  framecnt_t                   end_frame,
	                  boost::shared_ptr<Processor> endpoint,
	                  bool                         include_endpoint,
	                  bool                         for_export,
	                  bool                         for_freeze);

	int set_state (const XMLNode&, int version);

	void midi_panic(void);
	bool write_immediate_event(size_t size, const uint8_t* buf);

	/** A control that will send "immediate" events to a MIDI track when twiddled */
	struct MidiControl : public AutomationControl {
		MidiControl(MidiTrack* route, const Evoral::Parameter& param,
			    boost::shared_ptr<AutomationList> al = boost::shared_ptr<AutomationList>())
			: AutomationControl (route->session(), param, ParameterDescriptor(param), al)
			, _route (route)
		{}

		void set_value (double val, PBD::Controllable::GroupControlDisposition group_override);
		void set_value_unchecked (double);
		bool writable() const { return true; }

		MidiTrack* _route;

	private:
		void _set_value (double val, PBD::Controllable::GroupControlDisposition group_override);
	};

	virtual void set_parameter_automation_state (Evoral::Parameter param, AutoState);

	NoteMode note_mode() const { return _note_mode; }
	void set_note_mode (NoteMode m);

	std::string describe_parameter (Evoral::Parameter param);

	bool step_editing() const { return _step_editing; }
	void set_step_editing (bool yn);
	MidiRingBuffer<framepos_t>& step_edit_ring_buffer() { return _step_edit_ring_buffer; }

	PBD::Signal1<void,bool> StepEditStatusChange;

	boost::shared_ptr<SMFSource> write_source (uint32_t n = 0);

	/* Configure capture/playback channels (see MidiChannelFilter). */
	void set_capture_channel_mode (ChannelMode mode, uint16_t mask);
	void set_playback_channel_mode (ChannelMode mode, uint16_t mask);
	void set_playback_channel_mask (uint16_t mask);
	void set_capture_channel_mask (uint16_t mask);

	ChannelMode get_playback_channel_mode() const { return _playback_filter.get_channel_mode(); }
	ChannelMode get_capture_channel_mode()  const { return _capture_filter.get_channel_mode(); }
	uint16_t    get_playback_channel_mask() const { return _playback_filter.get_channel_mask(); }
	uint16_t    get_capture_channel_mask()  const { return _capture_filter.get_channel_mask(); }

	MidiChannelFilter& playback_filter() { return _playback_filter; }
	MidiChannelFilter& capture_filter()  { return _capture_filter; }

	boost::shared_ptr<MidiPlaylist> midi_playlist ();

	PBD::Signal1<void, boost::weak_ptr<MidiSource> > DataRecorded;
	boost::shared_ptr<MidiBuffer> get_gui_feed_buffer () const;

	void set_monitoring (MonitorChoice, PBD::Controllable::GroupControlDisposition);
	MonitorState monitoring_state () const;

	void set_input_active (bool);
	bool input_active () const;
	PBD::Signal0<void> InputActiveChanged;

protected:
	XMLNode& state (bool full);

	void act_on_mute ();

private:
	MidiRingBuffer<framepos_t> _immediate_events;
	MidiRingBuffer<framepos_t> _step_edit_ring_buffer;
	NoteMode                   _note_mode;
	bool                       _step_editing;
	bool                       _input_active;
	MidiChannelFilter          _playback_filter;
	MidiChannelFilter          _capture_filter;

	virtual boost::shared_ptr<Diskstream> diskstream_factory (XMLNode const &);

	boost::shared_ptr<MidiDiskstream> midi_diskstream () const;

	void write_out_of_band_data (BufferSet& bufs, framepos_t start_frame, framepos_t end_frame, framecnt_t nframes);

	void set_state_part_two ();
	void set_state_part_three ();


	int no_roll (pframes_t nframes, framepos_t start_frame, framepos_t end_frame, bool state_changing);
	void push_midi_input_to_step_edit_ringbuffer (framecnt_t nframes);

	void diskstream_data_recorded (boost::weak_ptr<MidiSource>);
	PBD::ScopedConnection _diskstream_data_recorded_connection;

	void track_input_active (IOChange, void*);
	void map_input_active (bool);

	/** Update automation controls to reflect any changes in buffers. */
	void update_controls (const BufferSet& bufs);
};

} /* namespace ARDOUR*/

#endif /* __ardour_midi_track_h__ */
