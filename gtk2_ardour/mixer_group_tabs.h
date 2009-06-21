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

#include "group_tabs.h"

class Mixer_UI;

class MixerGroupTabs : public GroupTabs
{
public:
	MixerGroupTabs (Mixer_UI *);

private:
	ARDOUR::RouteGroup* click_to_route_group (GdkEventButton *);
	void render (cairo_t *);
	
	void draw_group (cairo_t *, int32_t, int32_t, ARDOUR::RouteGroup* , Gdk::Color const &);
	
	Mixer_UI* _mixer;
};
