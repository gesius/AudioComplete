/*
    Copyright (C) 2007 Paul Davis
    Author: Dave Robillard

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

#include <iomanip>
#include <cmath>

#include "pbd/error.h"

#include "ardour/automation_list.h"
#include "ardour/automation_control.h"
#include "ardour/event_type_map.h"
#include "ardour/automatable.h"
#include "ardour/panner.h"
#include "ardour/pan_controllable.h"
#include "ardour/session.h"

#include "ardour_ui.h"
#include "utils.h"
#include "automation_controller.h"
#include "gui_thread.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace Gtk;

AutomationController::AutomationController(boost::shared_ptr<Automatable> owner, boost::shared_ptr<AutomationControl> ac, Adjustment* adj)
	: BarController (*adj, ac)
	, _ignore_change(false)
        , _owner (owner)
	, _controllable(ac)
	, _adjustment(adj)
{
	set_name (X_("PluginSlider")); // FIXME: get yer own name!
	set_style (BarController::LeftToRight);
	set_use_parent (true);

	StartGesture.connect (sigc::mem_fun(*this, &AutomationController::start_touch));
	StopGesture.connect (sigc::mem_fun(*this, &AutomationController::end_touch));

	_adjustment->signal_value_changed().connect (
			sigc::mem_fun(*this, &AutomationController::value_adjusted));

	_screen_update_connection = ARDOUR_UI::RapidScreenUpdate.connect (
			sigc::mem_fun (*this, &AutomationController::display_effective_value));

	ac->Changed.connect (_changed_connection, invalidator (*this), boost::bind (&AutomationController::value_changed, this), gui_context());
}

AutomationController::~AutomationController()
{
}

boost::shared_ptr<AutomationController>
AutomationController::create(
		boost::shared_ptr<Automatable> parent,
		const Evoral::Parameter& param,
		boost::shared_ptr<AutomationControl> ac)
{
	Gtk::Adjustment* adjustment = manage(new Gtk::Adjustment(param.normal(), param.min(), param.max()));
	if (!ac) {
		PBD::warning << "Creating AutomationController for " << EventTypeMap::instance().to_symbol(param) << endmsg;
		ac = boost::dynamic_pointer_cast<AutomationControl>(parent->control_factory(param));
	} else {
		assert(ac->parameter() == param);
	}
	return boost::shared_ptr<AutomationController>(new AutomationController(parent, ac, adjustment));
}

std::string
AutomationController::get_label (double& xpos)
{
        xpos = 0.5;
        return _owner->value_as_string (_controllable);
}

void
AutomationController::display_effective_value()
{
	float value = _controllable->get_value();

	if (_adjustment->get_value() != value) {
		_ignore_change = true;
		_adjustment->set_value (value);
		_ignore_change = false;
	}
}

void
AutomationController::value_adjusted()
{
	if (!_ignore_change) {
		_controllable->set_value(_adjustment->get_value());
	}
}

void
AutomationController::start_touch()
{
	_controllable->start_touch (_controllable->session().transport_frame());
}

void
AutomationController::end_touch ()
{
	if (_controllable->automation_state() == Touch) {

		bool mark = false;
		double when = 0;
                
		if (_controllable->session().transport_rolling()) {
			mark = true;
			when = _controllable->session().transport_frame();
		}

		_controllable->stop_touch (mark, when);
	}
}

void
AutomationController::automation_state_changed ()
{
	ENSURE_GUI_THREAD (*this, &AutomationController::automation_state_changed)

	bool x = (_controllable->automation_state() != Off);

	/* start watching automation so that things move */

	_screen_update_connection.disconnect();

	if (x) {
		_screen_update_connection = ARDOUR_UI::RapidScreenUpdate.connect (
				sigc::mem_fun (*this, &AutomationController::display_effective_value));
	}
}

void
AutomationController::value_changed ()
{
	Gtkmm2ext::UI::instance()->call_slot (invalidator (*this), boost::bind (&AutomationController::display_effective_value, this));
}

/** Stop updating our value from our controllable */
void
AutomationController::stop_updating ()
{
	_screen_update_connection.disconnect ();
}
