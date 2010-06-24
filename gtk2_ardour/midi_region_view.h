/*
    Copyright (C) 2001-2007 Paul Davis

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

#ifndef __gtk_ardour_midi_region_view_h__
#define __gtk_ardour_midi_region_view_h__

#include <string>
#include <vector>

#include <libgnomecanvasmm.h>
#include <libgnomecanvasmm/polygon.h>

#include "pbd/signals.h"

#include "ardour/midi_track.h"
#include "ardour/midi_region.h"
#include "ardour/midi_model.h"
#include "ardour/diskstream.h"
#include "ardour/types.h"

#include "editing.h"
#include "region_view.h"
#include "midi_time_axis.h"
#include "time_axis_view_item.h"
#include "automation_line.h"
#include "enums.h"
#include "canvas.h"
#include "canvas-hit.h"
#include "canvas-note.h"
#include "canvas-note-event.h"
#include "canvas-program-change.h"
#include "canvas-sysex.h"

namespace ARDOUR {
	class MidiRegion;
	class MidiModel;
	class Filter;
};

namespace MIDI {
	namespace Name {
		struct PatchPrimaryKey;
	};
};

class MidiTimeAxisView;
class GhostRegion;
class AutomationTimeAxisView;
class AutomationRegionView;
class MidiCutBuffer;
class MidiListEditor;

class MidiRegionView : public RegionView
{
  public:
	typedef Evoral::Note<Evoral::MusicalTime> NoteType;
	typedef Evoral::Sequence<Evoral::MusicalTime>::Notes Notes;

	MidiRegionView (ArdourCanvas::Group *,
	                RouteTimeAxisView&,
	                boost::shared_ptr<ARDOUR::MidiRegion>,
	                double initial_samples_per_unit,
	                Gdk::Color const & basic_color);

	MidiRegionView (const MidiRegionView& other);
	MidiRegionView (const MidiRegionView& other, boost::shared_ptr<ARDOUR::MidiRegion>);

	~MidiRegionView ();

	virtual void init (Gdk::Color const & basic_color, bool wfd);

	inline const boost::shared_ptr<ARDOUR::MidiRegion> midi_region() const
		{ return boost::dynamic_pointer_cast<ARDOUR::MidiRegion>(_region); }

	inline MidiTimeAxisView* midi_view() const
		{ return dynamic_cast<MidiTimeAxisView*>(&trackview); }

	inline MidiStreamView* midi_stream_view() const
		{ return midi_view()->midi_view(); }

	void add_note (uint8_t channel, uint8_t number, uint8_t velocity,
		       Evoral::MusicalTime pos, Evoral::MusicalTime len);

	void set_height (double);
	void apply_note_range(uint8_t lowest, uint8_t highest, bool force=false);

	inline ARDOUR::ColorMode color_mode() const { return midi_view()->color_mode(); }

	void set_frame_color();
        void color_handler ();

	void redisplay_model();

	GhostRegion* add_ghost (TimeAxisView&);

	void add_note(const boost::shared_ptr<NoteType> note, bool visible);
	void resolve_note(uint8_t note_num, double end_time);

	void cut_copy_clear (Editing::CutCopyOp);
	void paste (nframes64_t pos, float times, const MidiCutBuffer&);

	struct PCEvent {
		PCEvent(double a_time, uint8_t a_value, uint8_t a_channel)
			: time(a_time), value(a_value), channel(a_channel) {}

		double  time;
		uint8_t value;
		uint8_t channel;
	};

	/** Add a new program change flag to the canvas.
	 * @param program the MidiRegionView::PCEvent to add
	 * @param the text to display in the flag
	 */
	void add_pgm_change(PCEvent& program, const std::string& displaytext);

	/** Look up the given time and channel in the 'automation' and set keys accordingly.
	 * @param time the time of the program change event
	 * @param channel the MIDI channel of the event
	 * @key a reference to an instance of MIDI::Name::PatchPrimaryKey whose fields will
	 *        will be set according to the result of the lookup
	 */
	void get_patch_key_at(double time, uint8_t channel, MIDI::Name::PatchPrimaryKey& key);

	/** Change the 'automation' data of old_program to new values which correspond to new_patch.
	 * @param old_program the program change event which is to be altered
	 * @param new_patch the new lsb, msb and program number which are to be set
	 */
	void alter_program_change(PCEvent& old_program, const MIDI::Name::PatchPrimaryKey& new_patch);

	/** Alter a given program to the new given one.
	 * (Called on context menu select on CanvasProgramChange)
	 */
	void program_selected(
		ArdourCanvas::CanvasProgramChange& program,
		const MIDI::Name::PatchPrimaryKey& new_patch);

	/** Alter a given program to be its predecessor in the MIDNAM file.
	 */
	void previous_program(ArdourCanvas::CanvasProgramChange& program);

	/** Alters a given program to be its successor in the MIDNAM file.
	 */
	void next_program(ArdourCanvas::CanvasProgramChange& program);

	/** Displays all program change events in the region as flags on the canvas.
	 */
	void display_program_changes();

	/** Displays all system exclusive events in the region as flags on the canvas.
	 */
	void display_sysexes();

	void begin_write();
	void end_write();
	void extend_active_notes();

	void create_note_at(double x, double y, double length, bool);

	void display_model(boost::shared_ptr<ARDOUR::MidiModel> model);

	void start_diff_command(std::string name = "midi edit");
	void diff_add_change(ArdourCanvas::CanvasNoteEvent* ev, ARDOUR::MidiModel::DiffCommand::Property, uint8_t val);
	void diff_add_change(ArdourCanvas::CanvasNoteEvent* ev, ARDOUR::MidiModel::DiffCommand::Property, Evoral::MusicalTime val);
	void diff_add_note(const boost::shared_ptr<NoteType> note, bool selected, bool show_velocity=false);
	void diff_remove_note(ArdourCanvas::CanvasNoteEvent* ev);

	void apply_diff();
	void apply_diff_as_subcommand();
	void abort_command();

	void   note_entered(ArdourCanvas::CanvasNoteEvent* ev);
	void   note_left(ArdourCanvas::CanvasNoteEvent* ev);
	void   unique_select(ArdourCanvas::CanvasNoteEvent* ev);
	void   note_selected(ArdourCanvas::CanvasNoteEvent* ev, bool add, bool extend=false);
	void   note_deselected(ArdourCanvas::CanvasNoteEvent* ev);
	void   delete_selection();
	void   delete_note (boost::shared_ptr<NoteType>);
	size_t selection_size() { return _selection.size(); }

	void move_selection(double dx, double dy);
	void note_dropped(ArdourCanvas::CanvasNoteEvent* ev, double d_pixels, int8_t d_note);

	void select_matching_notes (uint8_t notenum, uint16_t channel_mask, bool add, bool extend);
	void toggle_matching_notes (uint8_t notenum, uint16_t channel_mask);

	/** Return true iff the note is within the extent of the region.
	 * @param visible will be set to true if the note is within the visible note range, false otherwise.
	 */
	bool note_in_region_range(const boost::shared_ptr<NoteType> note, bool& visible) const;

	/** Get the region position in pixels relative to session. */
	double get_position_pixels();

	/** Get the region end position in pixels relative to session. */
	double get_end_position_pixels();

	/** Begin resizing of some notes.
	 * Called by CanvasMidiNote when resizing starts.
	 * @param at_front which end of the note (true == note on, false == note off)
	 */
	void begin_resizing(bool at_front);

	void update_resizing (ArdourCanvas::CanvasNote *, bool, double, bool);
	void commit_resizing (ArdourCanvas::CanvasNote *, bool, double, bool);

	/** Adjust the velocity on a note, and the selection if applicable.
	 * @param velocity the relative or absolute velocity
	 * @param relative whether velocity is relative or absolute
	 */
	void change_velocity(ArdourCanvas::CanvasNoteEvent* ev, int8_t velocity, bool relative=false);

	/** Change the channel of the selection.
	 * @param channel - the channel number of the new channel, zero-based
	 */
	void change_channel(uint8_t channel);

	enum MouseState {
		None,
		Pressed,
		SelectTouchDragging,
		SelectRectDragging,
		AddDragging
	};

	MouseState mouse_state() const { return _mouse_state; }

	struct NoteResizeData {
		ArdourCanvas::CanvasNote  *canvas_note;
		ArdourCanvas::SimpleRect  *resize_rect;
	};

	/** Snap a region relative pixel coordinate to pixel units.
	 * @param x a pixel coordinate relative to region start
	 * @return the snapped pixel coordinate relative to region start
	 */
	double snap_to_pixel(double x);

	/** Snap a region relative pixel coordinate to frame units.
	 * @param x a pixel coordinate relative to region start
	 * @return the snapped nframes64_t coordinate relative to region start
	 */
	nframes64_t snap_pixel_to_frame(double x);

	/** Snap a region relative frame coordinate to frame units.
	 * @param x a pixel coordinate relative to region start
	 * @return the snapped nframes64_t coordinate relative to region start
	 */
	nframes64_t snap_frame_to_frame(nframes64_t x);

	/** Convert a timestamp in beats to frames (both relative to region start) */
	nframes64_t beats_to_frames(double beats) const;

	/** Convert a timestamp in frames to beats (both relative to region start) */
	double frames_to_beats(nframes64_t) const;

	void goto_previous_note ();
	void goto_next_note ();
	void change_note_lengths (bool, bool, bool start, bool end);
	void change_velocities (bool up, bool fine, bool allow_smush);
	void transpose (bool up, bool fine, bool allow_smush);
	void nudge_notes (bool forward);

	void show_list_editor ();

	void selection_as_notelist (Notes& selected, bool allow_all_if_none_selected = false);
        
  protected:
	/** Allows derived types to specify their visibility requirements
	 * to the TimeAxisViewItem parent class.
	 */
	MidiRegionView (ArdourCanvas::Group *,
	                RouteTimeAxisView&,
	                boost::shared_ptr<ARDOUR::MidiRegion>,
	                double samples_per_unit,
	                Gdk::Color& basic_color,
	                TimeAxisViewItem::Visibility);

	void region_resized (const PBD::PropertyChange&);

	void set_flags (XMLNode *);
	void store_flags ();

	void reset_width_dependent_items (double pixel_width);

  private:
	/** Play the NoteOn event of the given note immediately
	 * and schedule the playback of the corresponding NoteOff event.
	 */
	void play_midi_note(boost::shared_ptr<NoteType> note);

	/** Play the NoteOff-Event of the given note immediately
	 * (scheduled by @ref play_midi_note()).
	 */
	bool play_midi_note_off(boost::shared_ptr<NoteType> note);

	void clear_events();
	void switch_source(boost::shared_ptr<ARDOUR::Source> src);

	bool canvas_event(GdkEvent* ev);
	bool note_canvas_event(GdkEvent* ev);

	void midi_channel_mode_changed(ARDOUR::ChannelMode mode, uint16_t mask);
	void midi_patch_settings_changed(std::string model, std::string custom_device_mode);

	void change_note_velocity(ArdourCanvas::CanvasNoteEvent* ev, int8_t vel, bool relative=false);
	void change_note_note(ArdourCanvas::CanvasNoteEvent* ev, int8_t note, bool relative=false);
	void change_note_time(ArdourCanvas::CanvasNoteEvent* ev, ARDOUR::MidiModel::TimeType, bool relative=false);
	void trim_note(ArdourCanvas::CanvasNoteEvent* ev, ARDOUR::MidiModel::TimeType start_delta,
		       ARDOUR::MidiModel::TimeType end_delta);

	void clear_selection_except(ArdourCanvas::CanvasNoteEvent* ev);
	void clear_selection() { clear_selection_except(NULL); }
	void update_drag_selection(double last_x, double x, double last_y, double y);

	void add_to_selection (ArdourCanvas::CanvasNoteEvent*);
	void remove_from_selection (ArdourCanvas::CanvasNoteEvent*);

	int8_t   _force_channel;
	uint16_t _last_channel_selection;
	uint8_t  _current_range_min;
	uint8_t  _current_range_max;

	/// MIDNAM information of the current track: Model name of MIDNAM file
	std::string _model_name;

	/// MIDNAM information of the current track: CustomDeviceMode
	std::string _custom_device_mode;

	typedef std::list<ArdourCanvas::CanvasNoteEvent*> Events;
	typedef std::vector< boost::shared_ptr<ArdourCanvas::CanvasProgramChange> > PgmChanges;
	typedef std::vector< boost::shared_ptr<ArdourCanvas::CanvasSysEx> > SysExes;

	boost::shared_ptr<ARDOUR::MidiModel> _model;
	Events                               _events;
	PgmChanges                           _pgm_changes;
	SysExes                              _sys_exes;
	ArdourCanvas::CanvasNote**           _active_notes;
	ArdourCanvas::Group*                 _note_group;
	ARDOUR::MidiModel::DiffCommand*      _diff_command;
	ArdourCanvas::CanvasNote*            _ghost_note;
	double                               _last_ghost_x;
	double                               _last_ghost_y;
        double                               _drag_start_x;
        double                               _drag_start_y;
        double                               _last_x;
        double                               _last_y;
	ArdourCanvas::SimpleRect*            _drag_rect;

	MouseState _mouse_state;
	int _pressed_button;

	typedef std::set<ArdourCanvas::CanvasNoteEvent*> Selection;
	/// Currently selected CanvasNoteEvents
	Selection _selection;

	bool _sort_needed;
	void time_sort_events ();

	MidiCutBuffer* selection_as_cut_buffer () const;

	/** New notes (created in the current command) which should be selected
	 * when they appear after the command is applied. */
	std::set< boost::shared_ptr<NoteType> > _marked_for_selection;

	/** New notes (created in the current command) which should have visible velocity
	 * when they appear after the command is applied. */
	std::set< boost::shared_ptr<NoteType> > _marked_for_velocity;

	std::vector<NoteResizeData *> _resize_data;

	/* connection used to connect to model's ContentChanged signal */
	PBD::ScopedConnection content_connection;

	ArdourCanvas::CanvasNoteEvent* find_canvas_note (boost::shared_ptr<NoteType>);
	Events::iterator _optimization_iterator;

	void update_note (ArdourCanvas::CanvasNote*);
	void update_hit (ArdourCanvas::CanvasHit*);
	void create_ghost_note (double, double);
	void update_ghost_note (double, double);

	MidiListEditor* _list_editor;
	bool no_sound_notes;

        PBD::ScopedConnection note_delete_connection;
        void maybe_remove_deleted_note_from_selection (ArdourCanvas::CanvasNoteEvent*);

	void snap_changed ();
	PBD::ScopedConnection snap_changed_connection;

	void show_verbose_canvas_cursor (boost::shared_ptr<NoteType>) const;

        bool motion (GdkEventMotion*);
        bool scroll (GdkEventScroll*);
        bool key_press (GdkEventKey*);
        bool key_release (GdkEventKey*);
        bool button_press (GdkEventButton*);
        bool button_release (GdkEventButton*);
        bool enter_notify (GdkEventCrossing*);
        bool leave_notify (GdkEventCrossing*);

        void drop_down_keys ();
        void maybe_select_by_position (GdkEventButton* ev, double x, double y);
        void get_events (Events& e, Evoral::Sequence<Evoral::MusicalTime>::NoteOperator op, uint8_t val, int chan_mask = 0);
};


#endif /* __gtk_ardour_midi_region_view_h__ */
