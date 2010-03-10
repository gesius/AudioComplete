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

#include <gtkmm/box.h>
#include <gtkmm/table.h>

#include "gtkmm2ext/bindable_button.h"

#include "axis_view.h"
#include "level_meter.h"
#include "route_ui.h"

namespace Gtkmm2ext {
        class TearOff;
}

class VolumeController;

class MonitorSection : public RouteUI
{
  public:
        MonitorSection (ARDOUR::Session*);
        ~MonitorSection ();

        Gtk::Widget& pack_widget () const;
        void fast_update ();
        static void setup_knob_images ();

  private:
        Gtk::VBox vpacker;
        Gtk::HBox hpacker;
        Gtk::Table main_table;
        Gtk::VBox upper_packer;
        Gtk::VBox lower_packer;
        Gtk::VBox table_knob_packer;
        Gtk::VBox knob_packer;
        LevelMeter meter;
        Gtkmm2ext::TearOff* _tearoff;

        Gtk::Adjustment   gain_adjustment;
        VolumeController* gain_control;
        Gtk::Adjustment   dim_adjustment;
        VolumeController* dim_control;
        Gtk::Adjustment   solo_boost_adjustment;
        VolumeController* solo_boost_control;

        void populate_buttons ();
	void set_button_names ();
        void map_state ();

        boost::shared_ptr<ARDOUR::MonitorProcessor> _monitor;
        boost::shared_ptr<ARDOUR::Route> _route;

	static Glib::RefPtr<Gtk::ActionGroup> monitor_actions;
        void register_actions ();

        static Glib::RefPtr<Gdk::Pixbuf> big_knob_pixbuf;
        static Glib::RefPtr<Gdk::Pixbuf> little_knob_pixbuf;

        void cut_channel (uint32_t);
        void dim_channel (uint32_t);
        void solo_channel (uint32_t);
        void invert_channel (uint32_t);
        void dim_all ();
        void cut_all ();
        void mono ();
        void dim_level_changed ();
        void solo_boost_changed ();
        void gain_value_changed ();

        bool nonlinear_gain_printer (Gtk::SpinButton*);
        bool linear_gain_printer (Gtk::SpinButton*);

        Gtk::RadioButtonGroup solo_model_group;
        Gtk::RadioButton solo_in_place_button;
        Gtk::RadioButton afl_button;
        Gtk::RadioButton pfl_button;
        Gtk::HBox        solo_model_box;

        BindableToggleButton cut_all_button;
        BindableToggleButton dim_all_button;
};
