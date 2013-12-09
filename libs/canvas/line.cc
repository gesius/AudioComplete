/*
    Copyright (C) 2011-2013 Paul Davis
    Author: Carl Hetherington <cth@carlh.net>

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

#include <algorithm>
#include <cairomm/context.h>
#include "pbd/compose.h"
#include "canvas/line.h"
#include "canvas/types.h"
#include "canvas/debug.h"
#include "canvas/utils.h"
#include "canvas/canvas.h"

using namespace std;
using namespace ArdourCanvas;

Line::Line (Group* parent)
	: Item (parent)
	, Outline (parent)
{

}

void
Line::compute_bounding_box () const
{
	Rect bbox;
	
	bbox.x0 = min (_points[0].x, _points[1].x);
	bbox.y0 = min (_points[0].y, _points[1].y);
	bbox.x1 = max (_points[0].x, _points[1].x);
	bbox.y1 = max (_points[0].y, _points[1].y);

	bbox = bbox.expand (0.5 + (_outline_width / 2));

	_bounding_box = bbox;
	_bounding_box_dirty = false;
}

void
Line::render (Rect const & /*area*/, Cairo::RefPtr<Cairo::Context> context) const
{
	setup_outline_context (context);

	Duple p0 = item_to_window (Duple (_points[0].x, _points[0].y));
	Duple p1 = item_to_window (Duple (_points[1].x, _points[1].y));

	/* See Cairo FAQ on single pixel lines to understand why we add 0.5
	 */

	context->move_to (p0.x + 0.5, p0.y + 0.5);
	context->line_to (p1.x + 0.5, p1.y + 0.5);
	context->stroke ();
}

void
Line::set (Duple a, Duple b)
{
	begin_change ();

	_points[0] = a;
	_points[1] = b;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}

void
Line::set_x (Coord x0, Coord x1)
{
	begin_change ();
	
	_points[0].x = x0;
	_points[1].x = x1;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}	

void
Line::set_x0 (Coord x0)
{
	begin_change ();
	
	_points[0].x = x0;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}

void
Line::set_y0 (Coord y0)
{
	begin_change ();

	_points[0].y = y0;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}

void
Line::set_x1 (Coord x1)
{
	begin_change ();

	_points[1].x = x1;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}

void
Line::set_y1 (Coord y1)
{
	begin_change ();

	_points[1].y = y1;

	_bounding_box_dirty = true;
	end_change ();

	DEBUG_TRACE (PBD::DEBUG::CanvasItemsDirtied, "canvas item dirty: line change\n");
}

bool
Line::covers (Duple const & point) const
{
	const Duple p = canvas_to_item (point);
	static const Distance threshold = 2.0;

	/* this quick check works for vertical and horizontal lines, which are
	 * common.
	 */

	if (_points[0].x == _points[1].x) {
		/* line is vertical, just check x coordinate */
		return fabs (_points[0].x - p.x) <= threshold;
	}

	if (_points[0].y == _points[1].y) {
		/* line is horizontal, just check y coordinate */
		return fabs (_points[0].y - p.y) <= threshold;
	}

	Duple at;
	double t;
	Duple a (_points[0]);
	Duple b (_points[1]);
	const Rect visible (_canvas->visible_area());

	/*
	   Clamp the line endpoints to the visible area of the canvas. If we do
	   not do this, we have a line segment extending to COORD_MAX and our
	   math goes wrong.
	*/

	a.x = min (a.x, visible.x1);
	a.y = min (a.y, visible.y1);
	b.x = min (b.x, visible.x1);
	b.y = min (b.y, visible.y1);

	double d = distance_to_segment_squared (p, a, b, t, at);

	if (t < 0.0 || t > 1.0) {
		return false;
	}

	if (d < threshold) {
		return true;
	}

	return false;
}
