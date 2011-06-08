/*
    Copyright (C) 1999 Paul Davis

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

#ifndef __audio_clock_h__
#define __audio_clock_h__

#include <map>

#include <gtkmm/alignment.h>
#include <gtkmm/box.h>
#include <gtkmm/menu.h>
#include <gtkmm/label.h>

#include "ardour/ardour.h"
#include "ardour/session_handle.h"

class CairoEditableText;
class CairoCell;
class CairoTextCell;

namespace ARDOUR {
	class Session;
}

class AudioClock : public Gtk::VBox, public ARDOUR::SessionHandlePtr
{
  public:
	enum Mode {
		Timecode,
		BBT,
		MinSec,
		Frames
	};

	AudioClock (const std::string& clock_name, bool is_transient, const std::string& widget_name,
                    bool editable, bool follows_playhead, bool duration = false, bool with_info = false);
	~AudioClock ();

	Mode mode() const { return _mode; }
	void set_off (bool yn);
	bool off() const { return _off; }

	void focus ();

	void set (framepos_t, bool force = false, ARDOUR::framecnt_t offset = 0, char which = 0);
	void set_from_playhead ();
	void locate ();
	void set_mode (Mode);
	void set_bbt_reference (framepos_t);
        void set_is_duration (bool);

	void set_widget_name (const std::string&);

	std::string name() const { return _name; }

	framepos_t current_time (framepos_t position = 0) const;
	framepos_t current_duration (framepos_t position = 0) const;
	void set_session (ARDOUR::Session *s);

	sigc::signal<void> ValueChanged;
	sigc::signal<void> mode_changed;
	sigc::signal<void> ChangeAborted;

	static sigc::signal<void> ModeChanged;
	static std::vector<AudioClock*> clocks;

	static bool has_focus() { return _has_focus; }

	CairoEditableText& main_display () const { return *display; }
	CairoEditableText* supplemental_left_display () const { return supplemental_left; }
	CairoEditableText* supplemental_right_display () const { return supplemental_right; }

  private:
	Mode             _mode;
	uint32_t          key_entry_state;
	std::string      _name;
	bool              is_transient;
	bool              is_duration;
	bool              editable;
	/** true if this clock follows the playhead, meaning that certain operations are redundant */
	bool             _follows_playhead;
	bool             _off;

	Gtk::Menu  *ops_menu;

	CairoEditableText* display;

	enum Field {
		Timecode_Sign,
		Timecode_Hours,
		Timecode_Minutes,
		Timecode_Seconds,
		Timecode_Frames,
		MS_Hours,
		MS_Minutes,
		MS_Seconds,
		MS_Milliseconds,
		Bars,
		Beats,
		Ticks,
		AudioFrames,

		Colon1,
		Colon2,
		Colon3,
		Bar1,
		Bar2,

		LowerLeft1,
		LowerLeft2,
		LowerRight1,
		LowerRight2,
	};

	/** CairoCells of various kinds for each of our non-text Fields */
	std::map<Field,CairoCell*> _fixed_cells;
	/** CairoTextCells for each of our text Fields */
	std::map<Field, CairoTextCell*> _text_cells;
	CairoTextCell* label (Field) const;

	Gtk::HBox      off_hbox;
	
	CairoEditableText* supplemental_left;
	CairoEditableText* supplemental_right;

	Gtk::HBox top;
	Gtk::HBox bottom;

	Field editing_field;
	framepos_t bbt_reference_time;
	framepos_t last_when;
	bool last_pdelta;
	bool last_sdelta;

	uint32_t last_hrs;
	uint32_t last_mins;
	uint32_t last_secs;
	uint32_t last_frames;
	bool last_negative;

	long  ms_last_hrs;
	long  ms_last_mins;
	int   ms_last_secs;
	int   ms_last_millisecs;

	bool dragging;
	double drag_start_y;
	double drag_y;
	double drag_accum;

	/** true if the time of this clock is the one displayed in its widgets.
	 *  if false, the time in the widgets is an approximation of _canonical_time,
	 *  and _canonical_time should be returned as the `current' time of the clock.
	 */
	bool _canonical_time_is_displayed;
	framepos_t _canonical_time;

	void on_realize ();

	bool key_press (GdkEventKey *);
	bool key_release (GdkEventKey *);

	/* proxied from CairoEditableText */

	bool scroll (GdkEventScroll *ev, CairoCell*);
	bool button_press (GdkEventButton *ev, CairoCell*);
	bool button_release (GdkEventButton *ev, CairoCell*);
	sigc::connection scroll_connection;
	sigc::connection button_press_connection;
	sigc::connection button_release_connection;

	bool field_motion_notify_event (GdkEventMotion *ev, Field);
	bool field_focus_in_event (GdkEventFocus *, Field);
	bool field_focus_out_event (GdkEventFocus *, Field);
	bool drop_focus_handler (GdkEventFocus*);

	void set_timecode (framepos_t, bool);
	void set_bbt (framepos_t, bool);
	void set_minsec (framepos_t, bool);
	void set_frames (framepos_t, bool);

	framepos_t get_frames (Field, framepos_t pos = 0, int dir = 1);

	void timecode_sanitize_display();
	framepos_t timecode_frame_from_display () const;
	framepos_t bbt_frame_from_display (framepos_t) const;
	framepos_t bbt_frame_duration_from_display (framepos_t) const;
	framepos_t minsec_frame_from_display () const;
	framepos_t audio_frame_from_display () const;

	void build_ops_menu ();

	void session_configuration_changed (std::string);

	static uint32_t field_length[];
	static bool _has_focus;

	void on_style_changed (const Glib::RefPtr<Gtk::Style>&);
	bool on_key_press_event (GdkEventKey*);
	bool on_key_release_event (GdkEventKey*);

	void end_edit ();
	void edit_next_field ();

	void connect_signals ();
	void disconnect_signals ();

	void set_theme ();
};

#endif /* __audio_clock_h__ */
