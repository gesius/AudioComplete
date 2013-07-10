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

#include "ardour/meter.h"

#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/fastmeter.h>
#include <gtkmm2ext/barcontroller.h>
#include "midi++/manager.h"
#include "pbd/fastlog.h"

#include "ardour_ui.h"
#include "global_signals.h"
#include "level_meter.h"
#include "utils.h"
#include "logmeter.h"
#include "gui_thread.h"
#include "keyboard.h"
#include "public_editor.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Gtkmm2ext;
using namespace Gtk;
using namespace std;

//sigc::signal<void> LevelMeter::ResetAllPeakDisplays;
//sigc::signal<void,RouteGroup*> LevelMeter::ResetGroupPeakDisplays;


LevelMeter::LevelMeter (Session* s)
	: _meter (0)
	, meter_length (0)
	, thin_meter_width(2)
{
	set_session (s);
	set_spacing (1);
	Config->ParameterChanged.connect (_parameter_connection, invalidator (*this), boost::bind (&LevelMeter::parameter_changed, this, _1), gui_context());
	UI::instance()->theme_changed.connect (sigc::mem_fun(*this, &LevelMeter::on_theme_changed));
	ColorsChanged.connect (sigc::mem_fun (*this, &LevelMeter::color_handler));
	max_peak = minus_infinity();
}

void
LevelMeter::on_theme_changed()
{
	style_changed = true;
}

LevelMeter::~LevelMeter ()
{
	for (vector<MeterInfo>::iterator i = meters.begin(); i != meters.end(); i++) {
		delete (*i).meter;
	}
}

void
LevelMeter::set_meter (PeakMeter* meter)
{
	_configuration_connection.disconnect();
	_meter = meter;

	if (_meter) {
		_meter->ConfigurationChanged.connect (_configuration_connection, invalidator (*this), boost::bind (&LevelMeter::configuration_changed, this, _1, _2), gui_context());
	}
}

float
LevelMeter::update_meters ()
{
	vector<MeterInfo>::iterator i;
	uint32_t n;
	float peak, mpeak;

	if (!_meter) {
		return 0.0f;
	}

	uint32_t nmidi = _meter->input_streams().n_midi();

	for (n = 0, i = meters.begin(); i != meters.end(); ++i, ++n) {
		if ((*i).packed) {
			mpeak = _meter->max_peak_power(n);
			if (mpeak > (*i).max_peak) {
				(*i).max_peak = mpeak;
				(*i).meter->set_highlight(mpeak > Config->get_meter_peak());
			}
			if (mpeak > max_peak) {
				max_peak = mpeak;
			}

			peak = _meter->peak_power (n);
			if (n < nmidi) {
				(*i).meter->set (peak);
			} else {
				(*i).meter->set (log_meter (peak));
			}
		}
	}
	return max_peak;
}

void
LevelMeter::parameter_changed (string p)
{
	ENSURE_GUI_THREAD (*this, &LevelMeter::parameter_changed, p)

	if (p == "meter-hold") {
		vector<MeterInfo>::iterator i;
		uint32_t n;

		for (n = 0, i = meters.begin(); i != meters.end(); ++i, ++n) {
			(*i).meter->set_hold_count ((uint32_t) floor(Config->get_meter_hold()));
		}
	}
	else if (p == "meter-line-up-level") {
		color_changed = true;
		setup_meters (meter_length, regular_meter_width, thin_meter_width);
	}
	else if (p == "meter-peak") {
		vector<MeterInfo>::iterator i;
		uint32_t n;

		for (n = 0, i = meters.begin(); i != meters.end(); ++i, ++n) {
			(*i).max_peak = minus_infinity();
		}
	}
}

void
LevelMeter::configuration_changed (ChanCount /*in*/, ChanCount /*out*/)
{
	color_changed = true;
	setup_meters (meter_length, regular_meter_width, thin_meter_width);
}

void
LevelMeter::hide_all_meters ()
{
	for (vector<MeterInfo>::iterator i = meters.begin(); i != meters.end(); ++i) {
		if ((*i).packed) {
			remove (*((*i).meter));
			(*i).packed = false;
		}
	}
}

void
LevelMeter::setup_meters (int len, int initial_width, int thin_width)
{
	hide_all_meters ();

 	if (!_meter) {
 		return; /* do it later or never */
 	}

	int32_t nmidi = _meter->input_streams().n_midi();
	uint32_t nmeters = _meter->input_streams().n_total();
	regular_meter_width = initial_width;
	thin_meter_width = thin_width;
	meter_length = len;

	guint16 width;

	if (nmeters == 0) {
		return;
	}

	if (nmeters <= 2) {
		width = regular_meter_width;
	} else {
		width = thin_meter_width;
	}

	while (meters.size() < nmeters) {
		meters.push_back (MeterInfo());
	}

	//cerr << "LevelMeter::setup_meters() called color_changed = " << color_changed << " colors: " << endl;//DEBUG

	for (int32_t n = nmeters-1; nmeters && n >= 0 ; --n) {
		uint32_t c[10];
		float stp[4];
		if (n < nmidi) {
			c[0] = ARDOUR_UI::config()->canvasvar_MidiMeterColor0.get();
			c[1] = ARDOUR_UI::config()->canvasvar_MidiMeterColor1.get();
			c[2] = ARDOUR_UI::config()->canvasvar_MidiMeterColor2.get();
			c[3] = ARDOUR_UI::config()->canvasvar_MidiMeterColor3.get();
			c[4] = ARDOUR_UI::config()->canvasvar_MidiMeterColor4.get();
			c[5] = ARDOUR_UI::config()->canvasvar_MidiMeterColor5.get();
			c[6] = ARDOUR_UI::config()->canvasvar_MidiMeterColor6.get();
			c[7] = ARDOUR_UI::config()->canvasvar_MidiMeterColor7.get();
			c[8] = ARDOUR_UI::config()->canvasvar_MidiMeterColor8.get();
			c[9] = ARDOUR_UI::config()->canvasvar_MidiMeterColor9.get();
			stp[0] = 115.0 *  32.0 / 128.0;
			stp[1] = 115.0 *  64.0 / 128.0;
			stp[2] = 115.0 * 100.0 / 128.0;
			stp[3] = 115.0 * 112.0 / 128.0;
		} else {
			switch (Config->get_meter_line_up_level()) {
				case MeteringLineUp24:
					stp[0] = 42.0;
					stp[1] = 77.5;
					stp[2] = 92.5;
					stp[3] = 100.0;
					break;
				case MeteringLineUp20:
					stp[0] = 50.0;
					stp[1] = 77.5;
					stp[2] = 92.5;
					stp[3] = 100.0;
					break;
				default:
				case MeteringLineUp18:
					stp[0] = 55.0;
					stp[1] = 77.5;
					stp[2] = 92.5;
					stp[3] = 100.0;
					break;
				case MeteringLineUp15:
					stp[0] = 62.5;
					stp[1] = 77.5;
					stp[2] = 92.5;
					stp[3] = 100.0;
					break;
			}
			c[0] = ARDOUR_UI::config()->canvasvar_MeterColor0.get();
			c[1] = ARDOUR_UI::config()->canvasvar_MeterColor1.get();
			c[2] = ARDOUR_UI::config()->canvasvar_MeterColor2.get();
			c[3] = ARDOUR_UI::config()->canvasvar_MeterColor3.get();
			c[4] = ARDOUR_UI::config()->canvasvar_MeterColor4.get();
			c[5] = ARDOUR_UI::config()->canvasvar_MeterColor5.get();
			c[6] = ARDOUR_UI::config()->canvasvar_MeterColor6.get();
			c[7] = ARDOUR_UI::config()->canvasvar_MeterColor7.get();
			c[8] = ARDOUR_UI::config()->canvasvar_MeterColor8.get();
			c[9] = ARDOUR_UI::config()->canvasvar_MeterColor9.get();
		}
		if (meters[n].width != width || meters[n].length != len || color_changed) {
			delete meters[n].meter;
			meters[n].meter = new FastMeter ((uint32_t) floor (Config->get_meter_hold()), width, FastMeter::Vertical, len,
					c[0], c[1], c[2], c[3], c[4],
					c[5], c[6], c[7], c[8], c[9],
					ARDOUR_UI::config()->canvasvar_MeterBackgroundBot.get(),
					ARDOUR_UI::config()->canvasvar_MeterBackgroundTop.get(),
					0x991122ff, 0x551111ff,
					stp[0], stp[1], stp[2], stp[3]
					);
			meters[n].width = width;
			meters[n].length = len;
			meters[n].meter->add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK);
			meters[n].meter->signal_button_press_event().connect (sigc::mem_fun (*this, &LevelMeter::meter_button_press));
			meters[n].meter->signal_button_release_event().connect (sigc::mem_fun (*this, &LevelMeter::meter_button_release));
		}

		pack_end (*meters[n].meter, false, false);
		meters[n].meter->show_all ();
		meters[n].packed = true;
	}
	show();
	color_changed = false;
}

bool
LevelMeter::meter_button_press (GdkEventButton* ev)
{
	return ButtonPress (ev); /* EMIT SIGNAL */
}

bool
LevelMeter::meter_button_release (GdkEventButton* ev)
{
	if (ev->button == 1) {
		clear_meters ();
	}

	return true;
}


void LevelMeter::clear_meters ()
{
	for (vector<MeterInfo>::iterator i = meters.begin(); i < meters.end(); i++) {
		(*i).meter->clear();
		(*i).max_peak = minus_infinity();
		(*i).meter->set_highlight(false);
	}
	max_peak = minus_infinity();
}

void LevelMeter::hide_meters ()
{
	hide_all_meters();
}

void
LevelMeter::color_handler ()
{
	color_changed = true;
}

