/*
    Copyright (C) 2011 Paul Davis

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

#ifndef __ardour_gtk_speaker_dialog_h__
#define __ardour_gtk_speaker_dialog_h__

#include <gtkmm/drawingarea.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/box.h>
#include <gtkmm/adjustment.h>

#include "ardour/speakers.h"

#include "ardour_dialog.h"

class SpeakerDialog  : public ArdourDialog
{
  public:
    SpeakerDialog ();
    
    ARDOUR::Speakers get_speakers() const;
    void set_speakers (const ARDOUR::Speakers&);

  private:
    ARDOUR::Speakers speakers;
    Gtk::HBox        hbox;
    Gtk::VBox        side_vbox;
    Gtk::DrawingArea darea;
    Gtk::Adjustment  azimuth_adjustment;
    Gtk::SpinButton  azimuth_spinner;
    Gtk::Button      add_speaker_button;
    Gtk::Button      use_system_button;
    int32_t          selected_speaker;
    int              width;
    int              height;
    int              drag_x;
    int              drag_y;
    int              drag_index;

    bool darea_expose_event (GdkEventExpose*);
    void darea_size_allocate (Gtk::Allocation& alloc);
    bool darea_motion_notify_event (GdkEventMotion *ev);
    bool handle_motion (gint evx, gint evy, GdkModifierType state);
    bool darea_button_press_event (GdkEventButton *ev);
    bool darea_button_release_event (GdkEventButton *ev);

    void clamp_to_circle (double& x, double& y);
    void gtk_to_cart (PBD::CartesianVector& c) const;
    void cart_to_gtk (PBD::CartesianVector& c) const;
    int find_closest_object (gdouble x, gdouble y, int& which);
};

#endif /* __ardour_gtk_speaker_dialog_h__ */
