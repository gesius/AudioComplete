/*
    Copyright (C) 2009 Paul Davis

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

#ifndef __gtk2_ardour_editor_drag_h_
#define __gtk2_ardour_editor_drag_h_

#include <list>

#include <gdk/gdk.h>
#include <stdint.h>

#include "ardour/types.h"

#include "canvas.h"
#include "editor_items.h"

namespace ARDOUR {
	class Location;
}

namespace PBD {
	class StatefulDiffCommand;
}

namespace Gnome {
	namespace Canvas {
		class CanvasNoteEvent;
		class CanvasPatchChange;
	}
}

class Editor;
class EditorCursor;
class TimeAxisView;
class MidiTimeAxisView;
class Drag;

/** Class to manage current drags */
class DragManager
{
public:

	DragManager (Editor* e);
	~DragManager ();

	bool motion_handler (GdkEvent *, bool);

	void abort ();
	void add (Drag *);
	void set (Drag *, GdkEvent *, Gdk::Cursor* c = 0);
	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	bool end_grab (GdkEvent *);
	bool have_item (ArdourCanvas::Item *) const;

	/** @return true if an end drag or abort is in progress */
	bool ending () const {
		return _ending;
	}

	bool active () const {
		return !_drags.empty ();
	}

	/** @return current pointer x position in trackview coordinates */
	double current_pointer_x () const {
		return _current_pointer_x;
	}

	/** @return current pointer y position in trackview coordinates */
	double current_pointer_y () const {
		return _current_pointer_y;
	}

	/** @return current pointer frame */
	ARDOUR::framepos_t current_pointer_frame () const {
		return _current_pointer_frame;
	}

private:
	Editor* _editor;
	std::list<Drag*> _drags;
	bool _ending; ///< true if end_grab or abort is in progress, otherwise false
	double _current_pointer_x; ///< trackview x of the current pointer
	double _current_pointer_y; ///< trackview y of the current pointer
	ARDOUR::framepos_t _current_pointer_frame; ///< frame that the pointer is now at
	bool _old_follow_playhead; ///< state of Editor::follow_playhead() before the drags started
};

/** Abstract base class for dragging of things within the editor */
class Drag
{
public:
	Drag (Editor *, ArdourCanvas::Item *);
	virtual ~Drag () {}

	void set_manager (DragManager* m) {
		_drags = m;
	}

	/** @return the canvas item being dragged */
	ArdourCanvas::Item* item () const {
		return _item;
	}

	void swap_grab (ArdourCanvas::Item *, Gdk::Cursor *, uint32_t);
	bool motion_handler (GdkEvent*, bool);
	void abort ();

	ARDOUR::framepos_t adjusted_frame (ARDOUR::framepos_t, GdkEvent const *, bool snap = true) const;
	ARDOUR::framepos_t adjusted_current_frame (GdkEvent const *, bool snap = true) const;

	/** Called to start a grab of an item.
	 *  @param e Event that caused the grab to start.
	 *  @param c Cursor to use, or 0.
	 */
	virtual void start_grab (GdkEvent* e, Gdk::Cursor* c = 0);

	virtual bool end_grab (GdkEvent *);

	/** Called when a drag motion has occurred.
	 *  @param e Event describing the motion.
	 *  @param f true if this is the first movement, otherwise false.
	 */
	virtual void motion (GdkEvent* e, bool f) = 0;

	/** Called when a drag has finished.
	 *  @param e Event describing the finish.
	 *  @param m true if some movement occurred, otherwise false.
	 */
	virtual void finished (GdkEvent* e, bool m) = 0;

	/** Called to abort a drag and return things to how
	 *  they were before it started.
	 *  @param m true if some movement occurred, otherwise false.
	 */
	virtual void aborted (bool m) = 0;

	/** @param m Mouse mode.
	 *  @return true if this drag should happen in this mouse mode.
	 */
	virtual bool active (Editing::MouseMode m) {
		return (m != Editing::MouseGain);
	}

	/** @return minimum number of frames (in x) and pixels (in y) that should be considered a movement */
	virtual std::pair<ARDOUR::framecnt_t, int> move_threshold () const {
		return std::make_pair (1, 1);
	}

	virtual bool allow_vertical_autoscroll () const {
		return true;
	}

	/** @return true if x movement matters to this drag */
	virtual bool x_movement_matters () const {
		return true;
	}

	/** @return true if y movement matters to this drag */
	virtual bool y_movement_matters () const {
		return true;
	}

	/** Set up the _pointer_frame_offset */
	virtual void setup_pointer_frame_offset () {
		_pointer_frame_offset = 0;
	}

protected:

	double grab_x () const {
		return _grab_x;
	}

	double grab_y () const {
		return _grab_y;
	}

	ARDOUR::framepos_t raw_grab_frame () const {
		return _raw_grab_frame;
	}

	ARDOUR::framepos_t grab_frame () const {
		return _grab_frame;
	}

	double last_pointer_x () const {
		return _last_pointer_x;
	}

	double last_pointer_y () const {
		return _last_pointer_y;
	}

	double last_pointer_frame () const {
		return _last_pointer_frame;
	}

	void show_verbose_cursor_time (framepos_t);
	void show_verbose_cursor_duration (framepos_t, framepos_t, double xoffset = 0);
	void show_verbose_cursor_text (std::string const &);

	Editor* _editor; ///< our editor
	DragManager* _drags;
	ArdourCanvas::Item* _item; ///< our item
	/** Offset from the mouse's position for the drag to the start of the thing that is being dragged */
	ARDOUR::framecnt_t _pointer_frame_offset;
	bool _x_constrained; ///< true if x motion is constrained, otherwise false
	bool _y_constrained; ///< true if y motion is constrained, otherwise false
	bool _was_rolling; ///< true if the session was rolling before the drag started, otherwise false

private:

	bool _move_threshold_passed; ///< true if the move threshold has been passed, otherwise false
	double _grab_x; ///< trackview x of the grab start position
	double _grab_y; ///< trackview y of the grab start position
	double _last_pointer_x; ///< trackview x of the pointer last time a motion occurred
	double _last_pointer_y; ///< trackview y of the pointer last time a motion occurred
	ARDOUR::framepos_t _raw_grab_frame; ///< unsnapped frame that the mouse was at when start_grab was called, or 0
	ARDOUR::framepos_t _grab_frame; ///< adjusted_frame that the mouse was at when start_grab was called, or 0
	ARDOUR::framepos_t _last_pointer_frame; ///< adjusted_frame the last time a motion occurred
};

class RegionDrag;

/** Container for details about a region being dragged */
struct DraggingView
{
	DraggingView (RegionView *, RegionDrag *);

	RegionView* view; ///< the view
	/** index into RegionDrag::_time_axis_views of the view that this region is currently beind displayed on */
	int time_axis_view;
	/** layer that this region is currently being displayed on */
	ARDOUR::layer_t layer;
	double initial_y; ///< the initial y position of the view before any reparenting
	framepos_t initial_position; ///< initial position of the region
	framepos_t initial_end; ///< initial end position of the region
	boost::shared_ptr<ARDOUR::Playlist> initial_playlist;
};

/** Abstract base class for drags that involve region(s) */
class RegionDrag : public Drag, public sigc::trackable
{
public:
	RegionDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);
	virtual ~RegionDrag () {}

protected:

	RegionView* _primary; ///< the view that was clicked on (or whatever) to start the drag
	std::list<DraggingView> _views; ///< information about all views that are being dragged

	/** a list of the non-hidden TimeAxisViews sorted by editor order key */
	std::vector<TimeAxisView*> _time_axis_views;
	int find_time_axis_view (TimeAxisView *) const;

	int _visible_y_low;
	int _visible_y_high;

	friend class DraggingView;

private:

	void region_going_away (RegionView *);
	PBD::ScopedConnection death_connection;
};


/** Drags involving region motion from somewhere */
class RegionMotionDrag : public RegionDrag
{
public:

	RegionMotionDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &, bool);
	virtual ~RegionMotionDrag () {}

	virtual void start_grab (GdkEvent *, Gdk::Cursor *);
	virtual void motion (GdkEvent *, bool);
	virtual void finished (GdkEvent *, bool) = 0;
	virtual void aborted (bool);

	/** @return true if the regions being `moved' came from somewhere on the canvas;
	 *  false if they came from outside (e.g. from the region list).
	 */
	virtual bool regions_came_from_canvas () const = 0;

protected:

	double compute_x_delta (GdkEvent const *, ARDOUR::framecnt_t *);
	bool y_movement_allowed (int, ARDOUR::layer_t) const;

	bool _brushing;
	ARDOUR::framepos_t _last_frame_position; ///< last position of the thing being dragged
	double _total_x_delta;
	int _last_pointer_time_axis_view;
	ARDOUR::layer_t _last_pointer_layer;
};


/** Drags to move (or copy) regions that are already shown in the GUI to
 *  somewhere different.
 */
class RegionMoveDrag : public RegionMotionDrag
{
public:
	RegionMoveDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &, bool, bool);
	virtual ~RegionMoveDrag () {}

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool regions_came_from_canvas () const {
		return true;
	}

	std::pair<ARDOUR::framecnt_t, int> move_threshold () const {
		return std::make_pair (4, 4);
	}

	void setup_pointer_frame_offset ();

private:
	typedef std::set<boost::shared_ptr<ARDOUR::Playlist> > PlaylistSet;

	void finished_no_copy (
		bool const,
		bool const,
		ARDOUR::framecnt_t const
		);

	void finished_copy (
		bool const,
		bool const,
		ARDOUR::framecnt_t const
		);

	RegionView* insert_region_into_playlist (
		boost::shared_ptr<ARDOUR::Region>,
		RouteTimeAxisView*,
		ARDOUR::layer_t,
		ARDOUR::framecnt_t,
		PlaylistSet&
		);

	void remove_region_from_playlist (
		boost::shared_ptr<ARDOUR::Region>,
		boost::shared_ptr<ARDOUR::Playlist>,
		PlaylistSet& modified_playlists
		);

	void add_stateful_diff_commands_for_playlists (PlaylistSet const &);

	void collect_new_region_view (RegionView *);

	bool _copy;
	RegionView* _new_region_view;
};

/** Drag to insert a region from somewhere */
class RegionInsertDrag : public RegionMotionDrag
{
public:
	RegionInsertDrag (Editor *, boost::shared_ptr<ARDOUR::Region>, RouteTimeAxisView*, ARDOUR::framepos_t);

	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool regions_came_from_canvas () const {
		return false;
	}
};

/** Region drag in splice mode */
class RegionSpliceDrag : public RegionMoveDrag
{
public:
	RegionSpliceDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);
};

/** Drags to create regions */
class RegionCreateDrag : public Drag
{
public:
	RegionCreateDrag (Editor *, ArdourCanvas::Item *, TimeAxisView *);

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

private:
	MidiTimeAxisView* _view;
	boost::shared_ptr<ARDOUR::Region> _region;
        void add_region ();
};

/** Drags to resize MIDI notes */
class NoteResizeDrag : public Drag
{
public:
	NoteResizeDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

private:
	MidiRegionView*     region;
	bool                relative;
	bool                at_front;
};

/** Drags to move MIDI notes */
class NoteDrag : public Drag
{
  public:
	NoteDrag (Editor*, ArdourCanvas::Item*);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

  private:

	ARDOUR::frameoffset_t total_dx () const;
	int8_t total_dy () const;

	MidiRegionView* _region;
	Gnome::Canvas::CanvasNoteEvent* _primary;
	double _cumulative_dx;
	double _cumulative_dy;
	bool _was_selected;
	double _note_height;
};

/** Drag to move MIDI patch changes */
class PatchChangeDrag : public Drag
{
public:
	PatchChangeDrag (Editor *, ArdourCanvas::CanvasPatchChange *, MidiRegionView *);

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();

private:
	MidiRegionView* _region_view;
	ArdourCanvas::CanvasPatchChange* _patch_change;
	double _cumulative_dx;
};

/** Drag of region gain */
class RegionGainDrag : public Drag
{
public:
	RegionGainDrag (Editor *, ArdourCanvas::Item *);

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	bool active (Editing::MouseMode m) {
		return (m == Editing::MouseGain);
	}

	void aborted (bool);
};

/** Drag to trim region(s) */
class TrimDrag : public RegionDrag
{
public:
	enum Operation {
		StartTrim,
		EndTrim,
		ContentsTrim,
	};

	TrimDrag (Editor *, ArdourCanvas::Item *, RegionView*, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();

private:

	Operation _operation;
};

/** Meter marker drag */
class MeterMarkerDrag : public Drag
{
public:
	MeterMarkerDrag (Editor *, ArdourCanvas::Item *, bool);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();

private:
	MeterMarker* _marker;
	bool _copy;
};

/** Tempo marker drag */
class TempoMarkerDrag : public Drag
{
public:
	TempoMarkerDrag (Editor *, ArdourCanvas::Item *, bool);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();

private:
	TempoMarker* _marker;
	bool _copy;
};


/** Drag of the playhead cursor */
class CursorDrag : public Drag
{
public:
	CursorDrag (Editor *, ArdourCanvas::Item *, bool);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool active (Editing::MouseMode) {
		return true;
	}

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return true;
	}

private:
	void fake_locate (framepos_t);

	bool _stop; ///< true to stop the transport on starting the drag, otherwise false
	double _grab_zoom; ///< editor frames per unit when our grab started
};

/** Region fade-in drag */
class FadeInDrag : public RegionDrag
{
public:
	FadeInDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();
};

/** Region fade-out drag */
class FadeOutDrag : public RegionDrag
{
public:
	FadeOutDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();
};

/** Marker drag */
class MarkerDrag : public Drag
{
public:
	MarkerDrag (Editor *, ArdourCanvas::Item *);
	~MarkerDrag ();

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

	void setup_pointer_frame_offset ();

private:
	void update_item (ARDOUR::Location *);

	Marker* _marker; ///< marker being dragged
	std::list<ARDOUR::Location*> _copied_locations;
 	ArdourCanvas::Points _points;
};

/** Control point drag */
class ControlPointDrag : public Drag
{
public:
	ControlPointDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool active (Editing::MouseMode m);

private:

	ControlPoint* _point;
	double _fixed_grab_x;
	double _fixed_grab_y;
	double _cumulative_x_drag;
	double _cumulative_y_drag;
	static double const _zero_gain_fraction;
};

/** Gain or automation line drag */
class LineDrag : public Drag
{
public:
	LineDrag (Editor *e, ArdourCanvas::Item *i);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool active (Editing::MouseMode) {
		return true;
	}

private:

	AutomationLine* _line;
	double _fixed_grab_x;
	double _fixed_grab_y;
	uint32_t _before;
	uint32_t _after;
	double _cumulative_y_drag;
};

/** Transient feature line drags*/
class FeatureLineDrag : public Drag
{
public:
	FeatureLineDrag (Editor *e, ArdourCanvas::Item *i);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool active (Editing::MouseMode) {
		return true;
	}

private:

	ArdourCanvas::Line* _line;
	AudioRegionView* _arv;

	double _region_view_grab_x;
	double _cumulative_x_drag;

	float _before;
	uint32_t _max_x;
};

/** Dragging of a rubberband rectangle for selecting things */
class RubberbandSelectDrag : public Drag
{
public:
	RubberbandSelectDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	std::pair<ARDOUR::framecnt_t, int> move_threshold () const {
		return std::make_pair (8, 1);
	}
};

/** Region drag in time-FX mode */
class TimeFXDrag : public RegionDrag
{
public:
	TimeFXDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);
};

/** Scrub drag in audition mode */
class ScrubDrag : public Drag
{
public:
	ScrubDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);
};

/** Drag in range select mode */
class SelectionDrag : public Drag
{
public:
	enum Operation {
		CreateSelection,
		SelectionStartTrim,
		SelectionEndTrim,
		SelectionMove
	};

	SelectionDrag (Editor *, ArdourCanvas::Item *, Operation);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	void setup_pointer_frame_offset ();

private:
	Operation _operation;
	bool _copy;
	int _original_pointer_time_axis;
	int _last_pointer_time_axis;
	std::list<TimeAxisView*> _added_time_axes;
};

/** Range marker drag */
class RangeMarkerBarDrag : public Drag
{
public:
	enum Operation {
		CreateRangeMarker,
		CreateTransportMarker,
		CreateCDMarker
	};

	RangeMarkerBarDrag (Editor *, ArdourCanvas::Item *, Operation);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

private:
	void update_item (ARDOUR::Location *);

	Operation _operation;
	ArdourCanvas::SimpleRect* _drag_rect;
	bool _copy;
};

/** Drag of rectangle to set zoom */
class MouseZoomDrag : public Drag
{
public:
	MouseZoomDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	std::pair<ARDOUR::framecnt_t, int> move_threshold () const {
		return std::make_pair (4, 4);
	}

private:
	bool _zoom_out;
};

/** Drag of a range of automation data, changing value but not position */
class AutomationRangeDrag : public Drag
{
public:
	AutomationRangeDrag (Editor *, ArdourCanvas::Item *, std::list<ARDOUR::AudioRange> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted (bool);

	bool x_movement_matters () const {
		return false;
	}

private:
	std::list<ARDOUR::AudioRange> _ranges;
	AutomationTimeAxisView* _atav;

	/** A line that is part of the drag */
	struct Line {
		boost::shared_ptr<AutomationLine> line; ///< the line
		std::list<ControlPoint*> points; ///< points to drag on the line
		std::pair<ARDOUR::framepos_t, ARDOUR::framepos_t> range; ///< the range of all points on the line, in session frames
		XMLNode* state; ///< the XML state node before the drag
	};

	std::list<Line> _lines;

	bool _nothing_to_drag;
};

#endif /* __gtk2_ardour_editor_drag_h_ */

