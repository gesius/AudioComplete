/*
    Copyright (C) 2010 Paul Davis

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

#ifndef __gtk_ardour_stereo_panner_h__
#define __gtk_ardour_stereo_panner_h__

#include "pbd/signals.h"

#include <gtkmm/drawingarea.h>
#include <boost/shared_ptr.hpp>

namespace PBD {
        class Controllable;
}

class StereoPanner : public Gtk::DrawingArea
{
  public:
	StereoPanner (boost::shared_ptr<PBD::Controllable> pos, boost::shared_ptr<PBD::Controllable> width);
	~StereoPanner ();

  protected:
	bool on_expose_event (GdkEventExpose*);
	bool on_button_press_event (GdkEventButton*);
	bool on_button_release_event (GdkEventButton*);
	bool on_motion_notify_event (GdkEventMotion*);

  private:
        boost::shared_ptr<PBD::Controllable> position_control;
        boost::shared_ptr<PBD::Controllable> width_control;
        PBD::ScopedConnectionList connections;
        bool dragging;
        bool dragging_position;
        int drag_start_x;
        int last_drag_x;

        void value_change ();
        void set_tooltip ();
};

#endif /* __gtk_ardour_stereo_panner_h__ */
