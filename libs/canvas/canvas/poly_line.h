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

#ifndef __CANVAS_POLY_LINE_H__
#define __CANVAS_POLY_LINE_H__

#include "canvas/visibility.h"
#include "canvas/poly_item.h"
#include "canvas/outline.h"

namespace ArdourCanvas {

class LIBCANVAS_API PolyLine : public PolyItem
{
  public:
	PolyLine (Canvas*);
	PolyLine (Group*);
	
	void render (Rect const & area, Cairo::RefPtr<Cairo::Context>) const;
	
        bool covers (Duple const &) const;
	/**
	 * Set the distance at which a point will be considered to be covered
	 * by the line. For the definition of "distance" see
	 * utils.cc:distance_to_segment_squared()
	 */
	void set_covers_threshold (double);

  private:
	double _threshold;
};
	
}

#endif
