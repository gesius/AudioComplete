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

*/

#include <limits.h>

#include "ardour/amp.h"
#include "ardour/io.h"
#include "ardour/route.h"
#include "ardour/route_group.h"
#include "ardour/session.h"
#include "ardour/session_route.h"
#include "ardour/dB.h"
#include "ardour/utils.h"

#include <gtkmm/style.h>
#include <gdkmm/color.h>
#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/fastmeter.h>
#include <gtkmm2ext/barcontroller.h>
#include <gtkmm2ext/gtk_ui.h>
#include "midi++/manager.h"
#include "pbd/fastlog.h"
#include "pbd/stacktrace.h"

#include "ardour_ui.h"
#include "gain_meter.h"
#include "global_signals.h"
#include "logmeter.h"
#include "gui_thread.h"
#include "keyboard.h"
#include "public_editor.h"
#include "utils.h"

#include "ardour/session.h"
#include "ardour/route.h"
#include "ardour/meter.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Gtkmm2ext;
using namespace Gtk;
using namespace std;
using Gtkmm2ext::Keyboard;

sigc::signal<void> GainMeterBase::ResetAllPeakDisplays;
sigc::signal<void,RouteGroup*> GainMeterBase::ResetGroupPeakDisplays;

map<string,Glib::RefPtr<Gdk::Pixmap> > GainMeter::metric_pixmaps;
Glib::RefPtr<Gdk::Pixbuf> GainMeter::slider;


void
GainMeter::setup_slider_pix ()
{
	if ((slider = ::get_icon ("fader_belt")) == 0) {
		throw failed_constructor();
	}
}

GainMeterBase::GainMeterBase (Session* s,
			      const Glib::RefPtr<Gdk::Pixbuf>& pix,
			      bool horizontal,
			      int fader_length)
	: gain_adjustment (0.781787, 0.0, 1.0, 0.01, 0.1)  // 0.781787 is the value needed for gain to be set to 0.
	, gain_automation_style_button ("")
	, gain_automation_state_button ("")
	, style_changed (false)
	, dpi_changed (false)
	, _is_midi (false)

{
	using namespace Menu_Helpers;

	set_session (s);

	ignore_toggle = false;
	meter_menu = 0;
	next_release_selects = false;
	_width = Wide;

	if (horizontal) {
		gain_slider = manage (new HSliderController (pix,	
							     &gain_adjustment,
							     fader_length,
							     false));
	} else {
		gain_slider = manage (new VSliderController (pix,
							     &gain_adjustment,
							     fader_length,
							     false));
	}

	level_meter = new LevelMeter(_session);

	gain_slider->signal_button_press_event().connect (sigc::mem_fun(*this, &GainMeter::gain_slider_button_press));
	gain_slider->signal_button_release_event().connect (sigc::mem_fun(*this, &GainMeter::gain_slider_button_release));
	gain_slider->set_name ("GainFader");

	gain_display.set_name ("MixerStripGainDisplay");
	gain_display.set_has_frame (false);
	set_size_request_to_display_given_text (gain_display, "-80.g", 2, 6); /* note the descender */
	gain_display.signal_activate().connect (sigc::mem_fun (*this, &GainMeter::gain_activated));
	gain_display.signal_focus_in_event().connect (sigc::mem_fun (*this, &GainMeter::gain_focused), false);
	gain_display.signal_focus_out_event().connect (sigc::mem_fun (*this, &GainMeter::gain_focused), false);

	peak_display.set_name ("MixerStripPeakDisplay");
//	peak_display.set_has_frame (false);
//	peak_display.set_editable (false);
	set_size_request_to_display_given_text  (peak_display, "-80.g", 2, 6); /* note the descender */
	max_peak = minus_infinity();
	peak_display.set_label (_("-inf"));
	peak_display.unset_flags (Gtk::CAN_FOCUS);

	gain_automation_style_button.set_name ("MixerAutomationModeButton");
	gain_automation_state_button.set_name ("MixerAutomationPlaybackButton");

	ARDOUR_UI::instance()->set_tip (gain_automation_state_button, _("Fader automation mode"));
	ARDOUR_UI::instance()->set_tip (gain_automation_style_button, _("Fader automation type"));

	gain_automation_style_button.unset_flags (Gtk::CAN_FOCUS);
	gain_automation_state_button.unset_flags (Gtk::CAN_FOCUS);

	gain_automation_state_button.set_size_request(15, 15);
	gain_automation_style_button.set_size_request(15, 15);

	gain_astyle_menu.items().push_back (MenuElem (_("Trim")));
	gain_astyle_menu.items().push_back (MenuElem (_("Abs")));

	gain_astate_menu.set_name ("ArdourContextMenu");
	gain_astyle_menu.set_name ("ArdourContextMenu");

	gain_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &GainMeterBase::gain_adjusted));
	peak_display.signal_button_release_event().connect (sigc::mem_fun(*this, &GainMeterBase::peak_button_release), false);
	gain_display.signal_key_press_event().connect (sigc::mem_fun(*this, &GainMeterBase::gain_key_press), false);

	ResetAllPeakDisplays.connect (sigc::mem_fun(*this, &GainMeterBase::reset_peak_display));
	ResetGroupPeakDisplays.connect (sigc::mem_fun(*this, &GainMeterBase::reset_group_peak_display));

	UI::instance()->theme_changed.connect (sigc::mem_fun(*this, &GainMeterBase::on_theme_changed));
	ColorsChanged.connect (sigc::bind(sigc::mem_fun (*this, &GainMeterBase::color_handler), false));
	DPIReset.connect (sigc::bind(sigc::mem_fun (*this, &GainMeterBase::color_handler), true));
}

GainMeterBase::~GainMeterBase ()
{
	delete meter_menu;
	delete level_meter;
}

void
GainMeterBase::set_controls (boost::shared_ptr<Route> r,
			     boost::shared_ptr<PeakMeter> pm,
			     boost::shared_ptr<Amp> amp)
{
 	connections.clear ();
	model_connections.drop_connections ();

	if (!pm && !amp) {
		level_meter->set_meter (0);
		gain_slider->set_controllable (boost::shared_ptr<PBD::Controllable>());
		_meter.reset ();
		_amp.reset ();
		_route.reset ();
		return;
	}

	_meter = pm;
	_amp = amp;
	_route = r;

 	level_meter->set_meter (pm.get());
	gain_slider->set_controllable (amp->gain_control());

	if (!_route || _route->output()->n_ports().n_midi() == 0) {
		_is_midi = false;
		gain_adjustment.set_lower (0.0);
		gain_adjustment.set_upper (1.0);
		gain_adjustment.set_step_increment (0.01);
		gain_adjustment.set_page_increment (0.1);
	} else {
		_is_midi = true;
		gain_adjustment.set_lower (0.0);
		gain_adjustment.set_upper (2.0);
		gain_adjustment.set_step_increment (0.05);
		gain_adjustment.set_page_increment (0.1);
	}

	if (!_route || !_route->is_hidden()) {

		using namespace Menu_Helpers;

		gain_astate_menu.items().clear ();

		gain_astate_menu.items().push_back (MenuElem (_("Manual"),
							      sigc::bind (sigc::mem_fun (*(amp.get()), &Automatable::set_parameter_automation_state),
								    Evoral::Parameter(GainAutomation), (AutoState) Off)));
		gain_astate_menu.items().push_back (MenuElem (_("Play"),
							      sigc::bind (sigc::mem_fun (*(amp.get()), &Automatable::set_parameter_automation_state),
								    Evoral::Parameter(GainAutomation), (AutoState) Play)));
		gain_astate_menu.items().push_back (MenuElem (_("Write"),
							      sigc::bind (sigc::mem_fun (*(amp.get()), &Automatable::set_parameter_automation_state),
								    Evoral::Parameter(GainAutomation), (AutoState) Write)));
		gain_astate_menu.items().push_back (MenuElem (_("Touch"),
							      sigc::bind (sigc::mem_fun (*(amp.get()), &Automatable::set_parameter_automation_state),
								    Evoral::Parameter(GainAutomation), (AutoState) Touch)));

		connections.push_back (gain_automation_style_button.signal_button_press_event().connect (sigc::mem_fun(*this, &GainMeterBase::gain_automation_style_button_event), false));
		connections.push_back (gain_automation_state_button.signal_button_press_event().connect (sigc::mem_fun(*this, &GainMeterBase::gain_automation_state_button_event), false));

		boost::shared_ptr<AutomationControl> gc = amp->gain_control();
		
		gc->alist()->automation_state_changed.connect (model_connections, invalidator (*this), boost::bind (&GainMeter::gain_automation_state_changed, this), gui_context());
		gc->alist()->automation_style_changed.connect (model_connections, invalidator (*this), boost::bind (&GainMeter::gain_automation_style_changed, this), gui_context());
		
		gain_automation_state_changed ();
	}
	
	amp->gain_control()->Changed.connect (model_connections, invalidator (*this), boost::bind (&GainMeterBase::gain_changed, this), gui_context());

	gain_changed ();
	show_gain ();
	update_gain_sensitive ();
}

void
GainMeterBase::hide_all_meters ()
{
	level_meter->hide_meters();
}

void
GainMeter::hide_all_meters ()
{
	bool remove_metric_area = false;

	GainMeterBase::hide_all_meters ();

	if (remove_metric_area) {
		if (meter_metric_area.get_parent()) {
			level_meter->remove (meter_metric_area);
		}
	}
}

void
GainMeterBase::setup_meters (int len)
{
	level_meter->setup_meters(len, 5);
}

void
GainMeter::setup_meters (int len)
{
	if (!meter_metric_area.get_parent()) {
		level_meter->pack_end (meter_metric_area, false, false);
		meter_metric_area.show_all ();
	}
	GainMeterBase::setup_meters (len);
}

bool
GainMeterBase::gain_key_press (GdkEventKey* ev)
{
	if (key_is_legal_for_numeric_entry (ev->keyval)) {
		/* drop through to normal handling */
		return false;
	}
	/* illegal key for gain entry */
	return true;
}

bool
GainMeterBase::peak_button_release (GdkEventButton* ev)
{
	/* reset peak label */

	if (ev->button == 1 && Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier|Keyboard::TertiaryModifier)) {
		ResetAllPeakDisplays ();
	} else if (ev->button == 1 && Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
		if (_route) {
			ResetGroupPeakDisplays (_route->route_group());
		}
	} else {
		reset_peak_display ();
	}

	return true;
}

void
GainMeterBase::reset_peak_display ()
{
	_meter->reset_max();
	level_meter->clear_meters();
	max_peak = -INFINITY;
	peak_display.set_label (_("-Inf"));
	peak_display.set_name ("MixerStripPeakDisplay");
}

void
GainMeterBase::reset_group_peak_display (RouteGroup* group)
{
	if (_route && group == _route->route_group()) {
		reset_peak_display ();
		}
}

void
GainMeterBase::popup_meter_menu (GdkEventButton *ev)
{
	using namespace Menu_Helpers;

	if (meter_menu == 0) {
		meter_menu = new Gtk::Menu;
		MenuList& items = meter_menu->items();

		items.push_back (MenuElem ("-inf .. +0dBFS"));
		items.push_back (MenuElem ("-10dB .. +0dBFS"));
		items.push_back (MenuElem ("-4 .. +0dBFS"));
		items.push_back (SeparatorElem());
		items.push_back (MenuElem ("-inf .. -2dBFS"));
		items.push_back (MenuElem ("-10dB .. -2dBFS"));
		items.push_back (MenuElem ("-4 .. -2dBFS"));
	}

	meter_menu->popup (1, ev->time);
}

bool
GainMeterBase::gain_focused (GdkEventFocus* ev)
{
	if (ev->in) {
		gain_display.select_region (0, -1);
	} else {
		gain_display.select_region (0, 0);
	}
	return false;
}

void
GainMeterBase::gain_activated ()
{
	float f;

	if (sscanf (gain_display.get_text().c_str(), "%f", &f) == 1) {

		/* clamp to displayable values */

		f = min (f, 6.0f);

		_amp->set_gain (dB_to_coefficient(f), this);

		if (gain_display.has_focus()) {
			PublicEditor::instance().reset_focus();
		}
	}
}

void
GainMeterBase::show_gain ()
{
	char buf[32];

	float v = gain_adjustment.get_value();

	if (!_is_midi) {
		if (v == 0.0) {
			strcpy (buf, _("-inf"));
		} else {
			snprintf (buf, sizeof (buf), "%.1f", accurate_coefficient_to_dB (slider_position_to_gain (v)));
		}
	} else {
		snprintf (buf, sizeof (buf), "%.1f", v);
	}

	gain_display.set_text (buf);
}

void
GainMeterBase::gain_adjusted ()
{
	if (!ignore_toggle) {
		if (_route && _route->amp() == _amp) {
			if (_is_midi) {
				_route->set_gain (gain_adjustment.get_value(), this);
			} else {
				_route->set_gain (slider_position_to_gain (gain_adjustment.get_value()), this);
			}
		} else {
			_amp->set_gain (slider_position_to_gain (gain_adjustment.get_value()), this);
		}
	}

	show_gain ();
}

void
GainMeterBase::effective_gain_display ()
{
	gfloat value;

	if (!_route || _route->output()->n_ports().n_midi() == 0) {
		value = gain_to_slider_position (_amp->gain());
	} else {
		value = _amp->gain ();
	}

	//cerr << this << " for " << _io->name() << " EGAIN = " << value
	//		<< " AGAIN = " << gain_adjustment.get_value () << endl;
	// stacktrace (cerr, 20);

	if (gain_adjustment.get_value() != value) {
		ignore_toggle = true;
		gain_adjustment.set_value (value);
		ignore_toggle = false;
	}
}

void
GainMeterBase::gain_changed ()
{
	Gtkmm2ext::UI::instance()->call_slot (invalidator (*this), boost::bind (&GainMeterBase::effective_gain_display, this));
}

void
GainMeterBase::set_meter_strip_name (const char * name)
{
	meter_metric_area.set_name (name);
}

void
GainMeterBase::set_fader_name (const char * name)
{
	gain_slider->set_name (name);
}

void
GainMeterBase::update_gain_sensitive ()
{
	bool x = !(_amp->gain_control()->alist()->automation_state() & Play);
	static_cast<Gtkmm2ext::SliderController*>(gain_slider)->set_sensitive (x);
}

static MeterPoint
next_meter_point (MeterPoint mp)
{
	switch (mp) {
	case MeterInput:
		return MeterPreFader;
		break;

	case MeterPreFader:
		return MeterPostFader;
		break;

	case MeterPostFader:
		return MeterCustom;
		break;

	case MeterCustom:
		return MeterInput;		
		break;
	}

	/*NOTREACHED*/
	return MeterInput;
}

gint
GainMeterBase::meter_press(GdkEventButton* ev)
{
	wait_for_release = false;

	if (!_route) {
		return FALSE;
	}

	if (!ignore_toggle) {

		if (Keyboard::is_context_menu_event (ev)) {

			// no menu at this time.

		} else {

			if (Keyboard::is_button2_event(ev)) {

				// Primary-button2 click is the midi binding click
				// button2-click is "momentary"

				if (!Keyboard::modifier_state_equals (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier))) {
					wait_for_release = true;
					old_meter_point = _route->meter_point ();
				}
			}

			if (_route && (ev->button == 1 || Keyboard::is_button2_event (ev))) {

				if (Keyboard::modifier_state_equals (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::TertiaryModifier))) {

					/* Primary+Tertiary-click applies change to all routes */

					_session->foreach_route (this, &GainMeterBase::set_meter_point, next_meter_point (_route->meter_point()));


				} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {

					/* Primary-click: solo mix group.
					   NOTE: Primary-button2 is MIDI learn.
					*/

					if (ev->button == 1) {
						set_route_group_meter_point (*_route, next_meter_point (_route->meter_point()));
					}

				} else {

					/* click: change just this route */

					// XXX no undo yet

					_route->set_meter_point (next_meter_point (_route->meter_point()));
				}
			}
		}
	}

	return true;

}

gint
GainMeterBase::meter_release(GdkEventButton*)
{
	if(!ignore_toggle){
		if (wait_for_release){
			wait_for_release = false;

			if (_route) {
				set_meter_point (*_route, old_meter_point);
			}
		}
	}

	return true;
}

void
GainMeterBase::set_meter_point (Route& route, MeterPoint mp)
{
	route.set_meter_point (mp);
}

void
GainMeterBase::set_route_group_meter_point (Route& route, MeterPoint mp)
{
	RouteGroup* route_group;

	if ((route_group = route.route_group ()) != 0) {
		route_group->foreach_route (boost::bind (&Route::set_meter_point, _1, mp, false));
	} else {
		route.set_meter_point (mp);
	}
}

void
GainMeterBase::meter_point_clicked ()
{
	if (_route) {
		/* WHAT? */
	}
}

bool
GainMeterBase::gain_slider_button_press (GdkEventButton* ev)
{
	switch (ev->type) {
	case GDK_BUTTON_PRESS:
		_amp->gain_control()->start_touch (_amp->session().transport_frame());
		break;
	default:
		return false;
	}

	return true;
}

bool
GainMeterBase::gain_slider_button_release (GdkEventButton* ev)
{
	_amp->gain_control()->stop_touch (false, _amp->session().transport_frame());
	return true;
}

gint
GainMeterBase::gain_automation_state_button_event (GdkEventButton *ev)
{
	if (ev->type == GDK_BUTTON_RELEASE) {
		return TRUE;
	}

	switch (ev->button) {
		case 1:
			gain_astate_menu.popup (1, ev->time);
			break;
		default:
			break;
	}

	return TRUE;
}

gint
GainMeterBase::gain_automation_style_button_event (GdkEventButton *ev)
{
	if (ev->type == GDK_BUTTON_RELEASE) {
		return TRUE;
	}

	switch (ev->button) {
	case 1:
		gain_astyle_menu.popup (1, ev->time);
		break;
	default:
		break;
	}
	return TRUE;
}

string
GainMeterBase::astate_string (AutoState state)
{
	return _astate_string (state, false);
}

string
GainMeterBase::short_astate_string (AutoState state)
{
	return _astate_string (state, true);
}

string
GainMeterBase::_astate_string (AutoState state, bool shrt)
{
	string sstr;

	switch (state) {
	case Off:
		sstr = (shrt ? "M" : _("M"));
		break;
	case Play:
		sstr = (shrt ? "P" : _("P"));
		break;
	case Touch:
		sstr = (shrt ? "T" : _("T"));
		break;
	case Write:
		sstr = (shrt ? "W" : _("W"));
		break;
	}

	return sstr;
}

string
GainMeterBase::astyle_string (AutoStyle style)
{
	return _astyle_string (style, false);
}

string
GainMeterBase::short_astyle_string (AutoStyle style)
{
	return _astyle_string (style, true);
}

string
GainMeterBase::_astyle_string (AutoStyle style, bool shrt)
{
	if (style & Trim) {
		return _("Trim");
	} else {
	        /* XXX it might different in different languages */

		return (shrt ? _("Abs") : _("Abs"));
	}
}

void
GainMeterBase::gain_automation_style_changed ()
{
	switch (_width) {
	case Wide:
		gain_automation_style_button.set_label (astyle_string(_amp->gain_control()->alist()->automation_style()));
		break;
	case Narrow:
		gain_automation_style_button.set_label  (short_astyle_string(_amp->gain_control()->alist()->automation_style()));
		break;
	}
}

void
GainMeterBase::gain_automation_state_changed ()
{
	ENSURE_GUI_THREAD (*this, &GainMeterBase::gain_automation_state_changed)

	bool x;

	switch (_width) {
	case Wide:
		gain_automation_state_button.set_label (astate_string(_amp->gain_control()->alist()->automation_state()));
		break;
	case Narrow:
		gain_automation_state_button.set_label (short_astate_string(_amp->gain_control()->alist()->automation_state()));
		break;
	}

	x = (_amp->gain_control()->alist()->automation_state() != Off);

	if (gain_automation_state_button.get_active() != x) {
		ignore_toggle = true;
		gain_automation_state_button.set_active (x);
		ignore_toggle = false;
	}

	update_gain_sensitive ();

	/* start watching automation so that things move */

	gain_watching.disconnect();

	if (x) {
		gain_watching = ARDOUR_UI::RapidScreenUpdate.connect (sigc::mem_fun (*this, &GainMeterBase::effective_gain_display));
	}
}

void
GainMeterBase::update_meters()
{
	char buf[32];
	float mpeak = level_meter->update_meters();

	if (mpeak > max_peak) {
		max_peak = mpeak;
		if (mpeak <= -200.0f) {
			peak_display.set_label (_("-inf"));
		} else {
			snprintf (buf, sizeof(buf), "%.1f", mpeak);
			peak_display.set_label (buf);
		}

		if (mpeak >= 0.0f) {
			peak_display.set_name ("MixerStripPeakDisplayPeak");
		}
	}
}

void GainMeterBase::color_handler(bool dpi)
{
	color_changed = true;
	dpi_changed = (dpi) ? true : false;
	setup_meters();
}

void
GainMeterBase::set_width (Width w, int len)
{
	_width = w;
	level_meter->setup_meters (len);
}


void
GainMeterBase::on_theme_changed()
{
	style_changed = true;
}

GainMeter::GainMeter (Session* s, int fader_length)
	: GainMeterBase (s, slider, false, fader_length)
{
	gain_display_box.set_homogeneous (true);
	gain_display_box.set_spacing (2);
	gain_display_box.pack_start (gain_display, true, true);

	meter_metric_area.set_name ("AudioTrackMetrics");
	set_size_request_to_display_given_text (meter_metric_area, "-127", 0, 0);

	gain_automation_style_button.set_name ("MixerAutomationModeButton");
	gain_automation_state_button.set_name ("MixerAutomationPlaybackButton");

	ARDOUR_UI::instance()->set_tip (gain_automation_state_button, _("Fader automation mode"));
	ARDOUR_UI::instance()->set_tip (gain_automation_style_button, _("Fader automation type"));

	gain_automation_style_button.unset_flags (Gtk::CAN_FOCUS);
	gain_automation_state_button.unset_flags (Gtk::CAN_FOCUS);

	gain_automation_state_button.set_size_request(15, 15);
	gain_automation_style_button.set_size_request(15, 15);

	HBox* fader_centering_box = manage (new HBox);
	fader_centering_box->pack_start (*gain_slider, true, false);

	fader_vbox = manage (new Gtk::VBox());
	fader_vbox->set_spacing (0);
	fader_vbox->pack_start (*fader_centering_box, false, false, 0);

	hbox.set_spacing (2);
	hbox.pack_start (*fader_vbox, true, true);

	set_spacing (2);

	pack_start (gain_display_box, Gtk::PACK_SHRINK);
	pack_start (hbox, Gtk::PACK_SHRINK);

	meter_metric_area.signal_expose_event().connect (sigc::mem_fun(*this, &GainMeter::meter_metrics_expose));
}

void
GainMeter::set_controls (boost::shared_ptr<Route> r,
			 boost::shared_ptr<PeakMeter> meter,
			 boost::shared_ptr<Amp> amp)
{
	if (level_meter->get_parent()) {
		hbox.remove (*level_meter);
	}

	if (peak_display.get_parent()) {
		gain_display_box.remove (peak_display);
	}

	if (gain_automation_state_button.get_parent()) {
		fader_vbox->remove (gain_automation_state_button);
	}

	GainMeterBase::set_controls (r, meter, amp);

	if (_meter) {
		_meter->ConfigurationChanged.connect (
			model_connections, invalidator (*this), ui_bind (&GainMeter::meter_configuration_changed, this, _1), gui_context()
			);
		
		meter_configuration_changed (_meter->input_streams ());
	}

	
	/*
	   if we have a non-hidden route (ie. we're not the click or the auditioner),
	   pack some route-dependent stuff.
	*/

	gain_display_box.pack_end (peak_display, true, true);
	hbox.pack_end (*level_meter, true, true);

	if (r && !r->is_hidden()) {
		fader_vbox->pack_start (gain_automation_state_button, false, false, 0);
	}

	setup_meters ();
	hbox.show_all ();
}

int
GainMeter::get_gm_width ()
{
	Gtk::Requisition sz;
	hbox.size_request (sz);
	return sz.width;
}

Glib::RefPtr<Gdk::Pixmap>
GainMeter::render_metrics (Gtk::Widget& w, vector<DataType> types)
{
	Glib::RefPtr<Gdk::Window> win (w.get_window());
	Glib::RefPtr<Gdk::GC> bg_gc (w.get_style()->get_bg_gc (Gtk::STATE_NORMAL));

	gint width, height;
	win->get_size (width, height);

	Glib::RefPtr<Gdk::Pixmap> pixmap = Gdk::Pixmap::create (win, width, height);

	metric_pixmaps[w.get_name()] = pixmap;

	pixmap->draw_rectangle (bg_gc, true, 0, 0, width, height);

	Glib::RefPtr<Pango::Layout> layout = w.create_pango_layout ("");

	for (vector<DataType>::const_iterator i = types.begin(); i != types.end(); ++i) {

		Glib::RefPtr<Gdk::GC> fg_gc (w.get_style()->get_fg_gc (Gtk::STATE_NORMAL));
		
		if (types.size() > 1) {
			/* we're overlaying more than 1 set of marks, so use different colours */
			Gdk::Color c;
			switch (*i) {
			case DataType::AUDIO:
				c.set_rgb_p (1, 1, 1);
				break;
			case DataType::MIDI:
				c.set_rgb_p (0.2, 0.2, 0.5);
				break;
			}
			
			fg_gc->set_rgb_fg_color (c);
		}

		vector<int> points;
		
		switch (*i) {
		case DataType::AUDIO:
			points.push_back (-50);
			points.push_back (-40);
			points.push_back (-30);
			points.push_back (-20);
			points.push_back (-10);
			points.push_back (-3);
			points.push_back (0);
			points.push_back (4);
			break;
			
		case DataType::MIDI:
			points.push_back (0);
			if (types.size() == 1) {
				points.push_back (32);
			} else {
				/* tweak so as not to overlay the -30dB mark */				
				points.push_back (48);
			}
			points.push_back (64);
			points.push_back (96);
			points.push_back (127);
			break;
		}
		
		char buf[32];
		
		for (vector<int>::const_iterator j = points.begin(); j != points.end(); ++j) {
			
			float fraction = 0;
			switch (*i) {
			case DataType::AUDIO:
				fraction = log_meter (*j);
				break;
			case DataType::MIDI:
				fraction = *j / 127.0;
				break;
			}
			
			gint const pos = height - (gint) floor (height * fraction);
			
			snprintf (buf, sizeof (buf), "%d", abs (*j));
			
			layout->set_text (buf);
			
			/* we want logical extents, not ink extents here */
			
			int tw, th;
			layout->get_pixel_size (tw, th);
			
			pixmap->draw_line (fg_gc, 0, pos, 4, pos);
			
			int p = pos - (th / 2);
			p = min (p, height - th);
			p = max (p, 0);
			
			pixmap->draw_layout (fg_gc, 6, p, layout);
		}
	}

	return pixmap;
}

gint
GainMeter::meter_metrics_expose (GdkEventExpose *ev)
{
	Glib::RefPtr<Gdk::Window> win (meter_metric_area.get_window());
	Glib::RefPtr<Gdk::GC> bg_gc (meter_metric_area.get_style()->get_bg_gc (Gtk::STATE_INSENSITIVE));
	GdkRectangle base_rect;
	GdkRectangle draw_rect;
	gint width, height;

	win->get_size (width, height);

	base_rect.width = width;
	base_rect.height = height;
	base_rect.x = 0;
	base_rect.y = 0;

	Glib::RefPtr<Gdk::Pixmap> pixmap;
	std::map<string,Glib::RefPtr<Gdk::Pixmap> >::iterator i = metric_pixmaps.find (meter_metric_area.get_name());

	if (i == metric_pixmaps.end() || style_changed || dpi_changed) {
		pixmap = render_metrics (meter_metric_area, _types);
	} else {
		pixmap = i->second;
	}

	gdk_rectangle_intersect (&ev->area, &base_rect, &draw_rect);
	win->draw_drawable (bg_gc, pixmap, draw_rect.x, draw_rect.y, draw_rect.x, draw_rect.y, draw_rect.width, draw_rect.height);
	style_changed = false;
	return true;
}

boost::shared_ptr<PBD::Controllable>
GainMeterBase::get_controllable()
{
	if (_amp) {
		return _amp->gain_control();
	} else {
		return boost::shared_ptr<PBD::Controllable>();
	}
}

void
GainMeter::meter_configuration_changed (ChanCount c)
{
	_types.clear ();

	for (DataType::iterator i = DataType::begin(); i != DataType::end(); ++i) {
		if (c.get (*i) > 0) {
			_types.push_back (*i);
		}
	}

	style_changed = true;
	meter_metric_area.queue_draw ();
}
