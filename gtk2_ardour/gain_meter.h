/*
    Copyright (C) 2002 Paul Davis

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

    $Id$
*/

#ifndef __ardour_gtk_gain_meter_h__
#define __ardour_gtk_gain_meter_h__

#include <vector>
#include <map>

#include <gtkmm/box.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/frame.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/button.h>
#include <gtkmm/table.h>
#include <gtkmm/drawingarea.h>

#include <ardour/types.h>

#include <gtkmm2ext/slider_controller.h>
#include <gtkmm2ext/click_box.h>

#include "enums.h"

namespace ARDOUR {
	class IO;
	class Session;
	class Route;
	class RouteGroup;
}
namespace Gtkmm2ext {
	class FastMeter;
	class BarController;
}
namespace Gtk {
	class Menu;
}

class GainMeter : public Gtk::VBox
{
  public:
	GainMeter (ARDOUR::IO&, ARDOUR::Session&);
	~GainMeter ();

	void update_gain_sensitive ();

	void update_meters ();
	void update_meters_falloff ();

	void effective_gain_display ();

	void set_width (Width);
	void setup_meters ();

	void set_meter_strip_name (const char * name);
	void set_fader_name (const char * name);

  private:

	friend class MixerStrip;
	ARDOUR::IO& _io;
	ARDOUR::Session& _session;

	bool ignore_toggle;

	Gtkmm2ext::VSliderController *gain_slider;
	Gtk::Adjustment              gain_adjustment;
	Gtk::Frame                   gain_display_frame;
	Gtkmm2ext::ClickBox           gain_display;
	Gtk::Frame                   peak_display_frame;
	Gtk::EventBox                peak_display;
	Gtk::Label                   peak_display_label;
	Gtk::HBox                    gain_display_box;
	Gtk::HBox                    fader_box;
	Gtk::DrawingArea             meter_metric_area;

	sigc::connection gain_watching;

	Gtk::Button gain_automation_style_button;
	Gtk::ToggleButton gain_automation_state_button;

	Gtk::Menu gain_astate_menu;
	Gtk::Menu gain_astyle_menu;

	gint gain_automation_style_button_event (GdkEventButton *);
	gint gain_automation_state_button_event (GdkEventButton *);
	gint pan_automation_style_button_event (GdkEventButton *);
	gint pan_automation_state_button_event (GdkEventButton *);

	void gain_automation_state_changed();
	void gain_automation_style_changed();

	std::string astate_string (ARDOUR::AutoState);
	std::string short_astate_string (ARDOUR::AutoState);
	std::string _astate_string (ARDOUR::AutoState, bool);

	std::string astyle_string (ARDOUR::AutoStyle);
	std::string short_astyle_string (ARDOUR::AutoStyle);
	std::string _astyle_string (ARDOUR::AutoStyle, bool);

	Width                       _width;

	static std::map<std::string,Glib::RefPtr<Gdk::Pixmap> > metric_pixmaps;
	static Glib::RefPtr<Gdk::Pixmap> render_metrics (Gtk::Widget&);

	gint meter_metrics_expose (GdkEventExpose *);

	static void _gain_printer (char buf[32], Gtk::Adjustment&, void *);
	void gain_printer (char buf[32], Gtk::Adjustment&);
	
	struct MeterInfo {
	    Gtkmm2ext::FastMeter *meter;
	    gint16          width;   
	    bool            packed;
	    
	    MeterInfo() { 
		    meter = 0;
		    width = 0;
		    packed = false;
	    }
	};

	static const guint16 regular_meter_width = 5;
	static const guint16 thin_meter_width = 2;
	vector<MeterInfo>    meters;
	float       max_peak;
	
	Gtk::VBox*   fader_vbox;
	Gtk::HBox   hbox;
	Gtk::HBox   meter_packer;

	void gain_adjusted ();
	void gain_changed (void *);
	
	void meter_point_clicked ();
	void gain_unit_changed ();
	
	void hide_all_meters ();

	gint meter_button_press (GdkEventButton*, uint32_t);
	gint meter_button_release (GdkEventButton*, uint32_t);

	gint peak_button_release (GdkEventButton*);
	
	Gtk::Menu* meter_menu;
	void popup_meter_menu (GdkEventButton*);

	gint start_gain_touch (GdkEventButton*);
	gint end_gain_touch (GdkEventButton*);

	void set_mix_group_meter_point (ARDOUR::Route&, ARDOUR::MeterPoint);
	void set_meter_point (ARDOUR::Route&, ARDOUR::MeterPoint);
	gint meter_release (GdkEventButton*);
	gint meter_press (GdkEventButton*);
	bool wait_for_release;
	ARDOUR::MeterPoint old_meter_point;

	void meter_hold_changed();

	void reset_peak_display ();
	void reset_group_peak_display (ARDOUR::RouteGroup*);

	static sigc::signal<void> ResetAllPeakDisplays;
	static sigc::signal<void,ARDOUR::RouteGroup*> ResetGroupPeakDisplays;

	static Glib::RefPtr<Gdk::Pixbuf> slider;
	static Glib::RefPtr<Gdk::Pixbuf> rail;
	static int setup_slider_pix ();
};

#endif /* __ardour_gtk_gain_meter_h__ */

