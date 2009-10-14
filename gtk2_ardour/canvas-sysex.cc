/*
    Copyright (C) 2009 Paul Davis
    Author: Hans Baier

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

#include <iostream>

#include "ardour_ui.h"

#include "canvas-sysex.h"

using namespace Gnome::Canvas;
using namespace std;

CanvasSysEx::CanvasSysEx(
		MidiRegionView& region,
		Group&          parent,
		string&         text,
		double          height,
		double          x,
		double          y)
	: CanvasFlag(
			region,
			parent,
			height,
			ARDOUR_UI::config()->canvasvar_MidiSysExOutline.get(),
			ARDOUR_UI::config()->canvasvar_MidiSysExFill.get(),
			x,
			y)
{
	set_text(text);
}

CanvasSysEx::~CanvasSysEx()
{
}

bool
CanvasSysEx::on_event(GdkEvent* ev)
{
	switch (ev->type) {
	case GDK_BUTTON_PRESS:
		if (ev->button.button == 3) {
			return true;
		}
		break;

	case GDK_SCROLL:
		if (ev->scroll.direction == GDK_SCROLL_UP) {
			return true;
		} else if (ev->scroll.direction == GDK_SCROLL_DOWN) {
			return true;
		}
		break;

	default:
		break;
	}

	return false;
}

