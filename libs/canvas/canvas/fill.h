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

#ifndef __CANVAS_FILL_H__
#define __CANVAS_FILL_H__

#include <stdint.h>
#include "canvas/item.h"

namespace ArdourCanvas {

class Fill : virtual public Item
{
public:
	Fill (Group *);

	Color fill_color () const {
		return _fill_color;
	}
	void set_fill_color (Color);
	bool fill () const {
		return _fill;
	}
	void set_fill (bool);
	
protected:
	void setup_fill_context (Cairo::RefPtr<Cairo::Context>) const;
	
	Color _fill_color;
	bool _fill;
};

}

#endif
