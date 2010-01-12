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

class Editor;
class EditorCursor;
class TimeAxisView;

/** Abstract base class for dragging of things within the editor */
class Drag
{

public:
	Drag (Editor *, ArdourCanvas::Item *);
	virtual ~Drag () {}

	/** @return the canvas item being dragged */
	ArdourCanvas::Item* item () const {
		return _item;
	}

	void swap_grab (ArdourCanvas::Item *, Gdk::Cursor *, uint32_t);
	bool motion_handler (GdkEvent*, bool);
	void break_drag ();

	/** @return true if an end drag is in progress */
	bool ending () const {
		return _ending;
	}

	/** @return current pointer x position in trackview coordinates */
	double current_pointer_x () const {
		return _current_pointer_x;
	}

	/** @return current pointer y position in trackview coordinates */
	double current_pointer_y () const {
		return _current_pointer_y;
	}

	nframes64_t adjusted_frame (nframes64_t, GdkEvent const *, bool snap = true) const;
	nframes64_t adjusted_current_frame (GdkEvent const *, bool snap = true) const;
	
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
	 */
	virtual void aborted () = 0;

	/** @param m Mouse mode.
	 *  @return true if this drag should happen in this mouse mode.
	 */
	virtual bool active (Editing::MouseMode m) {
		return (m != Editing::MouseGain);
	}

	/** @return minimum number of frames (in x) and pixels (in y) that should be considered a movement */
	virtual std::pair<nframes64_t, int> move_threshold () const {
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

	/** @return current x extent of the thing being dragged; ie
	 *  a pair of (leftmost_position, rightmost_position)
	 */
	virtual std::pair<nframes64_t, nframes64_t> extent () const;

protected:

	double grab_x () const {
		return _grab_x;
	}

	double grab_y () const {
		return _grab_y;
	}

	double grab_frame () const {
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

	Editor* _editor; ///< our editor
	ArdourCanvas::Item* _item; ///< our item
	/** Offset from the mouse's position for the drag to the start of the thing that is being dragged */
	nframes64_t _pointer_frame_offset;
	bool _x_constrained; ///< true if x motion is constrained, otherwise false
	bool _y_constrained; ///< true if y motion is constrained, otherwise false
	bool _was_rolling; ///< true if the session was rolling before the drag started, otherwise false
	bool _have_transaction; ///< true if a transaction has been started, false otherwise. Must be set true by derived class.

private:

	bool _ending; ///< true if end_grab or break_drag is in progress, otherwise false
	bool _move_threshold_passed; ///< true if the move threshold has been passed, otherwise false
	double _grab_x; ///< trackview x of the grab start position
	double _grab_y; ///< trackview y of the grab start position
	double _current_pointer_x; ///< trackview x of the current pointer
	double _current_pointer_y; ///< trackview y of the current pointer
	double _last_pointer_x; ///< trackview x of the pointer last time a motion occurred
	double _last_pointer_y; ///< trackview y of the pointer last time a motion occurred
	nframes64_t _grab_frame; ///< adjusted_frame that the mouse was at when start_grab was called, or 0
	nframes64_t _last_pointer_frame; ///< adjusted_frame the last time a motion occurred
	nframes64_t _current_pointer_frame; ///< frame that the pointer is now at
};


/** Abstract base class for drags that involve region(s) */
class RegionDrag : public Drag, public sigc::trackable
{
public:
	RegionDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);
	virtual ~RegionDrag () {}

	std::pair<nframes64_t, nframes64_t> extent () const;

protected:

	RegionView* _primary; ///< the view that was clicked on (or whatever) to start the drag
	std::list<RegionView*> _views; ///< all views that are being dragged

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
	virtual void aborted ();

protected:
	struct TimeAxisViewSummary {
		TimeAxisViewSummary () : height_list(512) {}

		std::bitset<512> tracks;
		std::vector<int32_t> height_list;
		int visible_y_low;
		int visible_y_high;
	};

	void copy_regions (GdkEvent *);
	bool y_movement_disallowed (int, int, int, TimeAxisViewSummary const &) const;
	std::map<RegionView*, std::pair<RouteTimeAxisView*, int> > find_time_axis_views_and_layers ();
	double compute_x_delta (GdkEvent const *, nframes64_t *);
	bool compute_y_delta (
		TimeAxisView const *, TimeAxisView*, int32_t, int32_t, TimeAxisViewSummary const &,
		int32_t *, int32_t *, int32_t *
		);

	TimeAxisViewSummary get_time_axis_view_summary ();
	bool x_move_allowed () const;

	TimeAxisView* _dest_trackview;
	ARDOUR::layer_t _dest_layer;
	bool check_possible (RouteTimeAxisView **, ARDOUR::layer_t *);
	bool _brushing;
	nframes64_t _last_frame_position; ///< last position of the thing being dragged
	double _total_x_delta;
};


/** Drags to move (or copy) regions that are already shown in the GUI to
 *  somewhere different.
 */
class RegionMoveDrag : public RegionMotionDrag
{
public:
	RegionMoveDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &, bool, bool);
	virtual ~RegionMoveDrag () {}

	virtual void start_grab (GdkEvent *, Gdk::Cursor *);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	std::pair<nframes64_t, int> move_threshold () const {
		return std::make_pair (4, 4);
	}

private:
	bool _copy;
};

/** Drag to insert a region from somewhere */
class RegionInsertDrag : public RegionMotionDrag
{
public:
	RegionInsertDrag (Editor *, boost::shared_ptr<ARDOUR::Region>, RouteTimeAxisView*, nframes64_t);

	void finished (GdkEvent *, bool);
	void aborted ();
};

/** Region drag in splice mode */
class RegionSpliceDrag : public RegionMoveDrag
{
public:
	RegionSpliceDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();
};

/** Drags to create regions */
class RegionCreateDrag : public Drag
{
public:
	RegionCreateDrag (Editor *, ArdourCanvas::Item *, TimeAxisView *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

private:
	TimeAxisView* _view;
	TimeAxisView* _dest_trackview;
};

/** Drags to resize MIDI notes */
class NoteResizeDrag : public Drag
{
public:
	NoteResizeDrag (Editor *, ArdourCanvas::Item *);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

private:
	MidiRegionView*     region;
	bool                relative;
	bool                at_front;
};

class NoteDrag : public Drag
{
  public:
	NoteDrag (Editor*, ArdourCanvas::Item*);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

  private:
	MidiRegionView* region;
	double last_x;
	double last_y;
	double drag_delta_x;
	double drag_delta_note;
	bool   was_selected;
};

/** Drag of region gain */
class RegionGainDrag : public Drag
{
public:
	RegionGainDrag (Editor *e, ArdourCanvas::Item *i) : Drag (e, i) {}

	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	bool active (Editing::MouseMode m) {
		return (m == Editing::MouseGain);
	}

	void aborted ();
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
	void aborted ();

	bool y_movement_matters () const {
		return false;
	}

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
	void aborted ();

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}
	
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
	void aborted ();

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

private:
	TempoMarker* _marker;
	bool _copy;
};


/** Drag of a cursor */
class CursorDrag : public Drag
{
public:
	CursorDrag (Editor *, ArdourCanvas::Item *, bool);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	bool active (Editing::MouseMode) {
		return true;
	}

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}

private:
	EditorCursor* _cursor; ///< cursor being dragged
	bool _stop; ///< true to stop the transport on starting the drag, otherwise false

};

/** Region fade-in drag */
class FadeInDrag : public RegionDrag
{
public:
	FadeInDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	bool y_movement_matters () const {
		return false;
	}
};

/** Region fade-out drag */
class FadeOutDrag : public RegionDrag
{
public:
	FadeOutDrag (Editor *, ArdourCanvas::Item *, RegionView *, std::list<RegionView*> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	bool y_movement_matters () const {
		return false;
	}
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
	void aborted ();

	bool allow_vertical_autoscroll () const {
		return false;
	}

	bool y_movement_matters () const {
		return false;
	}
	
private:
	void update_item (ARDOUR::Location *);

	Marker* _marker; ///< marker being dragged
	std::list<ARDOUR::Location*> _copied_locations;
	ArdourCanvas::Line* _line;
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
	void aborted ();

	bool active (Editing::MouseMode m);

private:

	ControlPoint* _point;
	double _time_axis_view_grab_x;
	double _time_axis_view_grab_y;
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
	void aborted ();

	bool active (Editing::MouseMode) {
		return true;
	}

private:

	AutomationLine* _line;
	double _time_axis_view_grab_x;
	double _time_axis_view_grab_y;
	uint32_t _before;
	uint32_t _after;
	double _cumulative_y_drag;
};

/** Dragging of a rubberband rectangle for selecting things */
class RubberbandSelectDrag : public Drag
{
public:
	RubberbandSelectDrag (Editor *e, ArdourCanvas::Item *i) : Drag (e, i) {}

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	std::pair<nframes64_t, int> move_threshold () const {
		return std::make_pair (8, 1);
	}
};

/** Region drag in time-FX mode */
class TimeFXDrag : public RegionDrag
{
public:
	TimeFXDrag (Editor *e, ArdourCanvas::Item *i, RegionView* p, std::list<RegionView*> const & v) : RegionDrag (e, i, p, v) {}

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();
};

/** Scrub drag in audition mode */
class ScrubDrag : public Drag
{
public:
	ScrubDrag (Editor *e, ArdourCanvas::Item *i) : Drag (e, i) {}

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();
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
	void aborted ();

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
	void aborted ();

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
	MouseZoomDrag (Editor* e, ArdourCanvas::Item *i) : Drag (e, i) {}

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();
};

/** Drag of a range of automation data, changing value but not position */
class AutomationRangeDrag : public Drag
{
public:
	AutomationRangeDrag (Editor *, ArdourCanvas::Item *, std::list<ARDOUR::AudioRange> const &);

	void start_grab (GdkEvent *, Gdk::Cursor* c = 0);
	void motion (GdkEvent *, bool);
	void finished (GdkEvent *, bool);
	void aborted ();

	bool x_movement_matters () const {
		return false;
	}

private:
	std::list<ARDOUR::AudioRange> _ranges;
	AutomationTimeAxisView* _atav;
	boost::shared_ptr<AutomationLine> _line;
	bool _nothing_to_drag;
};

#endif /* __gtk2_ardour_editor_drag_h_ */

