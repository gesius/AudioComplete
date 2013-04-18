/*
    Copyright (C) 2013 Paul Davis

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
#include "canvas/circle.h"
#include "canvas/types.h"
#include "canvas/debug.h"
#include "canvas/utils.h"
#include "canvas/canvas.h"

using namespace std;
using namespace ArdourCanvas;

Circle::Circle (Group* parent)
	: Item (parent)
	, Outline (parent)
	, Fill (parent)
	, _radius (0.0)
{

}

void
Circle::compute_bounding_box () const
{
	Rect bbox;
	
	bbox.x0 = _center.x - _radius;
	bbox.y0 = _center.y - _radius;
	bbox.x1 = _center.x + _radius;
	bbox.y1 = _center.y + _radius;

	bbox = bbox.expand (0.5 + (_outline_width / 2));

	_bounding_box = bbox;
	_bounding_box_dirty = false;
}

void
Circle::render (Rect const & /*area*/, Cairo::RefPtr<Cairo::Context> context) const
{
	if (_radius <= 0.0) {
		return;
	}
	context->arc (_center.x, _center.y, _radius, 0.0, 2.0 * M_PI);
	setup_fill_context (context);
	context->fill_preserve ();
	setup_outline_context (context);
	context->stroke ();
}

void
Circle::set_center (Duple const & c)
{
	begin_change ();

	_center = c;

	_bounding_box_dirty = true;
	end_change ();
}

void
Circle::set_radius (Coord r)
{
	begin_change ();
	
	_radius = r;

	_bounding_box_dirty = true;
	end_change ();
}	

