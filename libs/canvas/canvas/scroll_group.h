/*
    Copyright (C) 2014 Paul Davis

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

#ifndef __CANVAS_SCROLL_GROUP_H__
#define __CANVAS_SCROLL_GROUP_H__

#include "canvas/group.h"

namespace ArdourCanvas {

class LIBCANVAS_API ScrollGroup : public Group
{
  public:
	enum ScrollSensitivity {
		ScrollsVertically = 0x1,
		ScrollsHorizontally = 0x2
	};
	
	explicit ScrollGroup (Group *, ScrollSensitivity);
	explicit ScrollGroup (Group *, Duple, ScrollSensitivity);

	void scroll_to (Duple const& d);
	Duple scroll_offset() const { return _scroll_offset; }

	bool covers_canvas (Duple const& d) const;
	bool covers_window (Duple const& d) const;

  private:
	ScrollSensitivity _scroll_sensitivity;
	Duple             _scroll_offset;
};

}

#endif
