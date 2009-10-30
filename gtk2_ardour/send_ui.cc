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

#include <gtkmm2ext/doi.h>

#include "ardour/amp.h"
#include "ardour/io.h"
#include "ardour/send.h"
#include "ardour/rc_configuration.h"

#include "utils.h"
#include "send_ui.h"
#include "io_selector.h"
#include "ardour_ui.h"
#include "gui_thread.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

SendUI::SendUI (Gtk::Window* parent, boost::shared_ptr<Send> s, Session& se)
	: _send (s)
	, _session (se)
	, _gpm (se)
	, _panners (se)
{
 	_panners.set_panner (s->panner());
 	_gpm.set_controls (boost::shared_ptr<Route>(), s->meter(), s->amp());

	_hbox.pack_start (_gpm, true, true);
	set_name ("SendUIFrame");

	_vbox.set_spacing (5);
	_vbox.set_border_width (5);

	_vbox.pack_start (_hbox, false, false, false);
	_vbox.pack_start (_panners, false,false);

	io = manage (new IOSelector (parent, se, s->output()));

	pack_start (_vbox, false, false);

	pack_start (*io, true, true);

	show_all ();

	_send->set_metering (true);

	_send->input()->changed.connect (mem_fun (*this, &SendUI::ins_changed));
	_send->output()->changed.connect (mem_fun (*this, &SendUI::outs_changed));

	_panners.set_width (Wide);
	_panners.setup_pan ();

	_gpm.setup_meters ();
	_gpm.set_fader_name ("SendUIFrame");

	// screen_update_connection = ARDOUR_UI::instance()->RapidScreenUpdate.connect (
	//		mem_fun (*this, &SendUI::update));
	fast_screen_update_connection = ARDOUR_UI::instance()->SuperRapidScreenUpdate.connect (
			mem_fun (*this, &SendUI::fast_update));
}

SendUI::~SendUI ()
{
	_send->set_metering (false);

	/* XXX not clear that we need to do this */

	screen_update_connection.disconnect();
	fast_screen_update_connection.disconnect();
}

void
SendUI::ins_changed (IOChange change, void* ignored)
{
	ENSURE_GUI_THREAD(bind (mem_fun (*this, &SendUI::ins_changed), change, ignored));
	if (change & ConfigurationChanged) {
		_panners.setup_pan ();
	}
}

void
SendUI::outs_changed (IOChange change, void* ignored)
{
	ENSURE_GUI_THREAD(bind (mem_fun (*this, &SendUI::outs_changed), change, ignored));
	if (change & ConfigurationChanged) {
		_panners.setup_pan ();
		_gpm.setup_meters ();
	}
}

void
SendUI::update ()
{
}

void
SendUI::fast_update ()
{
	if (Config->get_meter_falloff() > 0.0f) {
		_gpm.update_meters ();
	}
}

SendUIWindow::SendUIWindow (boost::shared_ptr<Send> s, Session& ss)
	: ArdourDialog (string("Ardour: send ") + s->name())
{
	ui = new SendUI (this, s, ss);

	hpacker.pack_start (*ui, true, true);

	get_vbox()->set_border_width (5);
	get_vbox()->pack_start (hpacker);

	set_name ("SendUIWindow");

	going_away_connection = s->GoingAway.connect (
			mem_fun (*this, &SendUIWindow::send_going_away));

	signal_delete_event().connect (bind (
					       sigc::ptr_fun (just_hide_it),
					       reinterpret_cast<Window *> (this)));
}

SendUIWindow::~SendUIWindow ()
{
	delete ui;
}

void
SendUIWindow::send_going_away ()
{
	ENSURE_GUI_THREAD (mem_fun (*this, &SendUIWindow::send_going_away));
	delete_when_idle (this);
	going_away_connection.disconnect ();
}

