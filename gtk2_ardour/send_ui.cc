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

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

SendUI::SendUI (Gtk::Window* parent, boost::shared_ptr<Send> s, Session* session)
	: _send (s)
	, _gpm (session, 250)
	, _panners (session)
{
	assert (_send);

 	_panners.set_panner (s->panner());
 	_gpm.set_controls (boost::shared_ptr<Route>(), s->meter(), s->amp());

	_hbox.pack_start (_gpm, true, true);
	set_name ("SendUIFrame");

	_vbox.set_spacing (5);
	_vbox.set_border_width (5);

	_vbox.pack_start (_hbox, false, false, false);
	_vbox.pack_start (_panners, false, false);

	io = manage (new IOSelector (parent, session, s->output()));

	pack_start (_vbox, false, false);

	pack_start (*io, true, true);

	show_all ();

	_send->set_metering (true);

	_send->output()->changed.connect (connections, invalidator (*this), ui_bind (&SendUI::outs_changed, this, _1, _2), gui_context());

	_panners.set_width (Wide);
	_panners.setup_pan ();

	_gpm.setup_meters ();
	_gpm.set_fader_name ("SendUIFrame");

	// screen_update_connection = ARDOUR_UI::instance()->RapidScreenUpdate.connect (
	//		sigc::mem_fun (*this, &SendUI::update));
	fast_screen_update_connection = ARDOUR_UI::instance()->SuperRapidScreenUpdate.connect (
			sigc::mem_fun (*this, &SendUI::fast_update));
}

SendUI::~SendUI ()
{
	_send->set_metering (false);

	/* XXX not clear that we need to do this */

	screen_update_connection.disconnect();
	fast_screen_update_connection.disconnect();
}

void
SendUI::outs_changed (IOChange change, void* /*ignored*/)
{
	ENSURE_GUI_THREAD (*this, &SendUI::outs_changed, change, ignored)
	if (change.type & IOChange::ConfigurationChanged) {
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

SendUIWindow::SendUIWindow (boost::shared_ptr<Send> s, Session* session)
	: ArdourDialog (string (_("Send ")) + s->name())
{
	ui = new SendUI (this, s, session);

	hpacker.pack_start (*ui, true, true);

	get_vbox()->set_border_width (5);
	get_vbox()->pack_start (hpacker);

	set_name ("SendUIWindow");

	s->DropReferences.connect (going_away_connection, invalidator (*this), boost::bind (&SendUIWindow::send_going_away, this), gui_context());

	signal_delete_event().connect (sigc::bind (
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
	ENSURE_GUI_THREAD (*this, &SendUIWindow::send_going_away)
	going_away_connection.disconnect ();
	delete_when_idle (this);
}

