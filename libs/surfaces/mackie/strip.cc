/*
	Copyright (C) 2006,2007 John Anderson
	Copyright (C) 2012 Paul Davis

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

#include <sstream>
#include <vector>

#include <stdint.h>

#include <sys/time.h>

#include <glibmm/convert.h>

#include "midi++/port.h"

#include "pbd/compose.h"
#include "pbd/convert.h"

#include "ardour/amp.h"
#include "ardour/bundle.h"
#include "ardour/debug.h"
#include "ardour/midi_ui.h"
#include "ardour/meter.h"
#include "ardour/plugin_insert.h"
#include "ardour/pannable.h"
#include "ardour/panner.h"
#include "ardour/panner_shell.h"
#include "ardour/rc_configuration.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/send.h"
#include "ardour/track.h"
#include "ardour/midi_track.h"
#include "ardour/user_bundle.h"
#include "ardour/profile.h"

#include "mackie_control_protocol.h"
#include "surface_port.h"
#include "surface.h"
#include "strip.h"
#include "button.h"
#include "led.h"
#include "pot.h"
#include "fader.h"
#include "jog.h"
#include "meter.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace ArdourSurface;
using namespace Mackie;

#ifndef timeradd /// only avail with __USE_BSD
#define timeradd(a,b,result)                         \
  do {                                               \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;    \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
    if ((result)->tv_usec >= 1000000)                \
    {                                                \
      ++(result)->tv_sec;                            \
      (result)->tv_usec -= 1000000;                  \
    }                                                \
  } while (0)
#endif

#define ui_context() MackieControlProtocol::instance() /* a UICallback-derived object that specifies the event loop for signal handling */

Strip::Strip (Surface& s, const std::string& name, int index, const map<Button::ID,StripButtonInfo>& strip_buttons)
	: Group (name)
	, _solo (0)
	, _recenable (0)
	, _mute (0)
	, _select (0)
	, _vselect (0)
	, _fader_touch (0)
	, _vpot (0)
	, _fader (0)
	, _meter (0)
	, _index (index)
	, _surface (&s)
	, _controls_locked (false)
	, _transport_is_rolling (false)
	, _metering_active (true)
	, _block_vpot_mode_redisplay_until (0)
	, _block_screen_redisplay_until (0)
	, eq_band (-1)
	, _pan_mode (PanAzimuthAutomation)
	, _trim_mode (TrimAutomation)
	, vpot_parameter (PanAzimuthAutomation)
	, _last_gain_position_written (-1.0)
	, _last_pan_azi_position_written (-1.0)
	, _last_pan_width_position_written (-1.0)
	, _last_trim_position_written (-1.0)
	, _current_send (0)
	, redisplay_requests (256)
{
	_fader = dynamic_cast<Fader*> (Fader::factory (*_surface, index, "fader", *this));
	_vpot = dynamic_cast<Pot*> (Pot::factory (*_surface, Pot::ID + index, "vpot", *this));

	if (s.mcp().device_info().has_meters()) {
		_meter = dynamic_cast<Meter*> (Meter::factory (*_surface, index, "meter", *this));
	}

	for (map<Button::ID,StripButtonInfo>::const_iterator b = strip_buttons.begin(); b != strip_buttons.end(); ++b) {
		Button* bb = dynamic_cast<Button*> (Button::factory (*_surface, b->first, b->second.base_id + index, b->second.name, *this));
		DEBUG_TRACE (DEBUG::MackieControl, string_compose ("surface %1 strip %2 new button BID %3 id %4 from base %5\n",
								   _surface->number(), index, Button::id_to_name (bb->bid()),
								   bb->id(), b->second.base_id));
	}
}

Strip::~Strip ()
{
	/* surface is responsible for deleting all controls */
}

void
Strip::add (Control & control)
{
	Button* button;

	Group::add (control);

	/* fader, vpot, meter were all set explicitly */

	if ((button = dynamic_cast<Button*>(&control)) != 0) {
		switch (button->bid()) {
		case Button::RecEnable:
			_recenable = button;
			break;
		case Button::Mute:
			_mute = button;
			break;
		case Button::Solo:
			_solo = button;
			break;
		case Button::Select:
			_select = button;
			break;
		case Button::VSelect:
			_vselect = button;
			break;
		case Button::FaderTouch:
			_fader_touch = button;
			break;
		default:
			break;
		}
	}
}

void
Strip::set_route (boost::shared_ptr<Route> r, bool /*with_messages*/)
{
	if (_controls_locked) {
		return;
	}

	mb_pan_controllable.reset();

	route_connections.drop_connections ();

	_solo->set_control (boost::shared_ptr<AutomationControl>());
	_mute->set_control (boost::shared_ptr<AutomationControl>());
	_select->set_control (boost::shared_ptr<AutomationControl>());
	_recenable->set_control (boost::shared_ptr<AutomationControl>());
	_fader->set_control (boost::shared_ptr<AutomationControl>());
	_vpot->set_control (boost::shared_ptr<AutomationControl>());

	_route = r;

	control_by_parameter.clear ();

	control_by_parameter[PanAzimuthAutomation] = (Control*) 0;
	control_by_parameter[PanWidthAutomation] = (Control*) 0;
	control_by_parameter[PanElevationAutomation] = (Control*) 0;
	control_by_parameter[PanFrontBackAutomation] = (Control*) 0;
	control_by_parameter[PanLFEAutomation] = (Control*) 0;
	control_by_parameter[GainAutomation] = (Control*) 0;
	control_by_parameter[TrimAutomation] = (Control*) 0;
	control_by_parameter[PhaseAutomation] = (Control*) 0;
	control_by_parameter[SendAutomation] = (Control*) 0;

	reset_saved_values ();

	if (!r) {
		zero ();
		return;
	}

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("Surface %1 strip %2 now mapping route %3\n",
							   _surface->number(), _index, _route->name()));

	_solo->set_control (_route->solo_control());
	_mute->set_control (_route->mute_control());

	_route->solo_changed.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_solo_changed, this), ui_context());
	_route->listen_changed.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_solo_changed, this), ui_context());

	_route->mute_control()->Changed.connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_mute_changed, this), ui_context());

	if (_route->trim() && route()->trim()->active()) {
		_route->trim_control()->Changed.connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_trim_changed, this, false), ui_context());
	}

	if (_route->phase_invert().size()) {
		_route->phase_invert_changed.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_phase_changed, this, false), ui_context());
		_route->phase_control()->set_channel(0);
	}

	boost::shared_ptr<AutomationControl> pan_control = _route->pan_azimuth_control();
	if (pan_control) {
		pan_control->Changed.connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_panner_azi_changed, this, false), ui_context());
	}

	pan_control = _route->pan_width_control();
	if (pan_control) {
		pan_control->Changed.connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_panner_width_changed, this, false), ui_context());
	}

	_route->gain_control()->Changed.connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_gain_changed, this, false), ui_context());
	_route->PropertyChanged.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_property_changed, this, _1), ui_context());

	boost::shared_ptr<Track> trk = boost::dynamic_pointer_cast<ARDOUR::Track>(_route);

	if (trk) {
		_recenable->set_control (trk->rec_enable_control());
		trk->rec_enable_control()->Changed .connect(route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_record_enable_changed, this), ui_context());
	}

	// TODO this works when a currently-banked route is made inactive, but not
	// when a route is activated which should be currently banked.

	_route->active_changed.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_active_changed, this), ui_context());
	_route->DropReferences.connect (route_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_route_deleted, this), ui_context());

	/* setup legal VPot modes for this route */

	possible_pot_parameters.clear();

	if (_route->pan_azimuth_control()) {
		possible_pot_parameters.push_back (PanAzimuthAutomation);
	}
	if (_route->pan_width_control()) {
		possible_pot_parameters.push_back (PanWidthAutomation);
	}
	if (_route->pan_elevation_control()) {
		possible_pot_parameters.push_back (PanElevationAutomation);
	}
	if (_route->pan_frontback_control()) {
		possible_pot_parameters.push_back (PanFrontBackAutomation);
	}
	if (_route->pan_lfe_control()) {
		possible_pot_parameters.push_back (PanLFEAutomation);
	}

	if (_route->trim() && route()->trim()->active()) {
		possible_pot_parameters.push_back (TrimAutomation);
	}

	possible_trim_parameters.clear();

	if (_route->trim() && route()->trim()->active()) {
		possible_trim_parameters.push_back (TrimAutomation);
		_trim_mode = TrimAutomation;
	}

	if (_route->phase_invert().size()) {
		possible_trim_parameters.push_back (PhaseAutomation);
		_route->phase_control()->set_channel(0);
		if (_trim_mode != TrimAutomation) {
			_trim_mode = PhaseAutomation;
		}
	}
	_current_send = 0;
	/* Update */
	_pan_mode = PanAzimuthAutomation;
	potmode_changed (false);
	notify_all ();

}

void
Strip::notify_all()
{
	if (!_route) {
		zero ();
		return;
	}
	// The active V-pot control may not be active for this strip
	// But if we zero it in the controls function it may erase
	// the one we do want
	_surface->write (_vpot->zero());

	notify_solo_changed ();
	notify_mute_changed ();
	notify_gain_changed ();
	notify_property_changed (PBD::PropertyChange (ARDOUR::Properties::name));
	notify_panner_azi_changed ();
	notify_panner_width_changed ();
	notify_record_enable_changed ();
	notify_trim_changed ();
	notify_phase_changed ();
	notify_processor_changed ();
}

void
Strip::notify_solo_changed ()
{
	if (_route && _solo) {
		_surface->write (_solo->set_state ((_route->soloed() || _route->listening_via_monitor()) ? on : off));
	}
}

void
Strip::notify_mute_changed ()
{
	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("Strip %1 mute changed\n", _index));
	if (_route && _mute) {
		DEBUG_TRACE (DEBUG::MackieControl, string_compose ("\troute muted ? %1\n", _route->muted()));
		DEBUG_TRACE (DEBUG::MackieControl, string_compose ("mute message: %1\n", _mute->set_state (_route->muted() ? on : off)));

		_surface->write (_mute->set_state (_route->muted() ? on : off));
	}
}

void
Strip::notify_record_enable_changed ()
{
	if (_route && _recenable)  {
		_surface->write (_recenable->set_state (_route->record_enabled() ? on : off));
	}
}

void
Strip::notify_active_changed ()
{
	_surface->mcp().refresh_current_bank();
}

void
Strip::notify_route_deleted ()
{
	_surface->mcp().refresh_current_bank();
}

void
Strip::notify_gain_changed (bool force_update)
{
	if (_route) {

		Control* control;

		if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
			control = _vpot;
		} else {
			control = _fader;
		}

		boost::shared_ptr<AutomationControl> ac = _route->gain_control();

		float gain_coefficient = ac->get_value();
		float normalized_position = ac->internal_to_interface (gain_coefficient);


		if (force_update || normalized_position != _last_gain_position_written) {

			if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
				if (!control->in_use()) {
					_surface->write (_vpot->set (normalized_position, true, Pot::wrap));
				}
				queue_parameter_display (GainAutomation, gain_coefficient);
			} else {
				if (!control->in_use()) {
					_surface->write (_fader->set_position (normalized_position));
				}
				queue_parameter_display (GainAutomation, gain_coefficient);
			}

			_last_gain_position_written = normalized_position;
		}
	}
}

void
Strip::notify_trim_changed (bool force_update)
{
	if (_route) {

		if (!_route->trim() || !route()->trim()->active()) {
			return;
		}
		Control* control = 0;
		ControlParameterMap::iterator i = control_by_parameter.find (TrimAutomation);

		if (i == control_by_parameter.end()) {
			return;
		}

		control = i->second;

		boost::shared_ptr<AutomationControl> ac = _route->trim_control();

		float gain_coefficient = ac->get_value();
		float normalized_position = ac->internal_to_interface (gain_coefficient);

		if (force_update || normalized_position != _last_trim_position_written) {
			if (control == _fader) {
				if (!_fader->in_use()) {
					_surface->write (_fader->set_position (normalized_position));
					queue_parameter_display (TrimAutomation, gain_coefficient);
				}
			} else if (control == _vpot) {
				_surface->write (_vpot->set (normalized_position, true, Pot::dot));
				queue_parameter_display (TrimAutomation, gain_coefficient);
			}
			_last_trim_position_written = normalized_position;
		}
	}
}

void
Strip::notify_phase_changed (bool force_update)
{
	if (_route) {
		if (!_route->phase_invert().size()) {
			return;
		}

		Control* control = 0;
		ControlParameterMap::iterator i = control_by_parameter.find (PhaseAutomation);

		if (i == control_by_parameter.end()) {
			return;
		}

		control = i->second;

		float normalized_position = _route->phase_control()->get_value();

		if (control == _fader) {
			if (!_fader->in_use()) {
				_surface->write (_fader->set_position (normalized_position));
				queue_parameter_display (PhaseAutomation, normalized_position);
			}
		} else if (control == _vpot) {
			_surface->write (_vpot->set (normalized_position, true, Pot::wrap));
			queue_parameter_display (PhaseAutomation, normalized_position);
		}
	}
}

void
Strip::notify_processor_changed (bool force_update)
{
	if (_route) {
		boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
		if (!p) {
			return;
		}

		Control* control = 0;
		ControlParameterMap::iterator i = control_by_parameter.find (SendAutomation);

		if (i == control_by_parameter.end()) {
			return;
		}

		control = i->second;

		boost::shared_ptr<Send> s =  boost::dynamic_pointer_cast<Send>(p);
		boost::shared_ptr<Amp> a = s->amp();
		boost::shared_ptr<AutomationControl> ac = a->gain_control();

		float gain_coefficient = ac->get_value();
		float normalized_position = ac->internal_to_interface (gain_coefficient);

		if (control == _fader) {
			if (!_fader->in_use()) {
				_surface->write (_fader->set_position (normalized_position));
				queue_parameter_display (SendAutomation, gain_coefficient);
			}
		} else if (control == _vpot) {
			_surface->write (_vpot->set (normalized_position, true, Pot::dot));
			queue_parameter_display (SendAutomation, gain_coefficient);
		}
	}
}

void
Strip::notify_property_changed (const PropertyChange& what_changed)
{
	if (!what_changed.contains (ARDOUR::Properties::name)) {
		return;
	}

	show_route_name ();
}

void
Strip::show_route_name ()
{
	MackieControlProtocol::SubViewMode svm = _surface->mcp().subview_mode();

	if (svm != MackieControlProtocol::None) {
		/* subview mode is responsible for upper line */
		return;
	}

	string fullname = string();
	if (!_route) {
		// make sure first three strips get cleared of view mode
		if (_index > 2) {
			return;
		}
	} else {
		fullname = _route->name();
	}
	string line1;

	if (fullname.length() <= 6) {
		line1 = fullname;
	} else {
		line1 = PBD::short_version (fullname, 6);
	}

	_surface->write (display (0, line1));
}

void
Strip::notify_eq_change (AutomationType type, uint32_t band, bool force_update)
{
	boost::shared_ptr<Route> r = _surface->mcp().subview_route();

	if (!r) {
		/* not in subview mode */
		return;
	}

	if (_surface->mcp().subview_mode() != MackieControlProtocol::EQ) {
		/* no longer in EQ subview mode */
		return;
	}

	boost::shared_ptr<AutomationControl> control;

	switch (type) {
	case EQGain:
		control = r->eq_gain_controllable (band);
		break;
	case EQFrequency:
		control = r->eq_freq_controllable (band);
		break;
	case EQQ:
		control = r->eq_q_controllable (band);
		break;
	case EQShape:
		control = r->eq_shape_controllable (band);
		break;
	case EQHPF:
		control = r->eq_hpf_controllable ();
		break;
	case EQEnable:
		control = r->eq_enable_controllable ();
		break;
	default:
		break;
	}

	if (control) {
		float val = control->get_value();
		queue_parameter_display (type, val);
		/* update pot/encoder */
		_surface->write (_vpot->set (control->internal_to_interface (val), true, Pot::wrap));
	}
}

void
Strip::notify_dyn_change (AutomationType type, bool force_update, bool propagate_mode)
{
	boost::shared_ptr<Route> r = _surface->mcp().subview_route();

	if (!r) {
		/* not in subview mode */
		return;
	}

	if (_surface->mcp().subview_mode() != MackieControlProtocol::Dynamics) {
		/* no longer in EQ subview mode */
		return;
	}

	boost::shared_ptr<AutomationControl> control;
	bool reset_all = false;

	switch (type) {
	case CompThreshold:
		control = r->comp_threshold_controllable ();
		break;
	case CompSpeed:
		control = r->comp_speed_controllable ();
		break;
	case CompMode:
		control = r->comp_mode_controllable ();
		reset_all = true;
		break;
	case CompMakeup:
		control = r->comp_makeup_controllable ();
		break;
	case CompRedux:
		control = r->comp_redux_controllable ();
		break;
	case CompEnable:
		control = r->comp_enable_controllable ();
		break;
	default:
		break;
	}

	if (propagate_mode && reset_all) {
		_surface->subview_mode_changed ();
	}

	if (control) {
		float val = control->get_value();
		queue_parameter_display (type, val);
		/* update pot/encoder */
		_surface->write (_vpot->set (control->internal_to_interface (val), true, Pot::wrap));
	}
}

void
Strip::notify_panner_azi_changed (bool force_update)
{
	if (!_route) {
		return;
	}

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("pan change for strip %1\n", _index));

	boost::shared_ptr<AutomationControl> pan_control = _route->pan_azimuth_control ();

	if (!pan_control) {
		return;
	}

	Control* control = 0;
	ControlParameterMap::iterator i = control_by_parameter.find (PanAzimuthAutomation);

	if (i == control_by_parameter.end()) {
		return;
	}

	control = i->second;

	double normalized_pos = pan_control->internal_to_interface (pan_control->get_value());
	double internal_pos = pan_control->get_value();

	if (force_update || (normalized_pos != _last_pan_azi_position_written)) {

		if (control == _fader) {
			if (!_fader->in_use()) {
				_surface->write (_fader->set_position (normalized_pos));
				/* show actual internal value to user */
				queue_parameter_display (PanAzimuthAutomation, internal_pos);
			}
		} else if (control == _vpot) {
			_surface->write (_vpot->set (normalized_pos, true, Pot::dot));
			/* show actual internal value to user */
			queue_parameter_display (PanAzimuthAutomation, internal_pos);
		}

		_last_pan_azi_position_written = normalized_pos;
	}
}

void
Strip::notify_panner_width_changed (bool force_update)
{
	if (!_route) {
		return;
	}

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("pan width change for strip %1\n", _index));

	boost::shared_ptr<AutomationControl> pan_control = _route->pan_width_control ();

	if (!pan_control) {
		return;
	}

	Control* control = 0;
	ControlParameterMap::iterator i = control_by_parameter.find (PanWidthAutomation);

	if (i == control_by_parameter.end()) {
		return;
	}

	control = i->second;

	double pos = pan_control->internal_to_interface (pan_control->get_value());

	if (force_update || pos != _last_pan_width_position_written) {

		if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {

			if (control == _fader) {
				if (!control->in_use()) {
					_surface->write (_fader->set_position (pos));
					queue_parameter_display (PanWidthAutomation, pos);
				}
			}

		} else if (control == _vpot) {
			_surface->write (_vpot->set (pos, true, Pot::spread));
			queue_parameter_display (PanWidthAutomation, pos);
		}

		_last_pan_width_position_written = pos;
	}
}

void
Strip::select_event (Button&, ButtonState bs)
{
	DEBUG_TRACE (DEBUG::MackieControl, "select button\n");

	if (bs == press) {

		int ms = _surface->mcp().main_modifier_state();

		if (ms & MackieControlProtocol::MODIFIER_CMDALT) {
			_controls_locked = !_controls_locked;
			_surface->write (display (1,_controls_locked ?  "Locked" : "Unlock"));
			block_vpot_mode_display_for (1000);
			return;
		}

		if (ms & MackieControlProtocol::MODIFIER_SHIFT) {
			/* reset to default */
			boost::shared_ptr<AutomationControl> ac = _fader->control ();
			if (ac) {
				ac->set_value (ac->normal(), Controllable::NoGroup);
			}
			return;
		}

		DEBUG_TRACE (DEBUG::MackieControl, "add select button on press\n");
		_surface->mcp().add_down_select_button (_surface->number(), _index);
		_surface->mcp().select_range ();

	} else {
		DEBUG_TRACE (DEBUG::MackieControl, "remove select button on release\n");
		_surface->mcp().remove_down_select_button (_surface->number(), _index);
	}
}

void
Strip::vselect_event (Button&, ButtonState bs)
{
	if (_surface->mcp().subview_mode() != MackieControlProtocol::None) {

		/* subview mode: vpot press acts like a button for toggle parameters */

		if (bs != press) {
			return;
		}

		boost::shared_ptr<AutomationControl> control = _vpot->control ();
		if (!control) {
			return;
		}

		if (control->toggled()) {
			if (control->toggled()) {
				control->set_value (!control->get_value(), Controllable::NoGroup);
			}
		}

		return;
	}

	if (bs == press) {

		int ms = _surface->mcp().main_modifier_state();

		if (ms & MackieControlProtocol::MODIFIER_SHIFT) {

			boost::shared_ptr<AutomationControl> ac = _vpot->control ();

			if (ac) {

				/* reset to default/normal value */
				ac->set_value (ac->normal(), Controllable::NoGroup);
			}

		}  else {

			DEBUG_TRACE (DEBUG::MackieControl, "switching to next pot mode\n");
			next_pot_mode ();
		}

	}
}

void
Strip::fader_touch_event (Button&, ButtonState bs)
{
	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("fader touch, press ? %1\n", (bs == press)));

	if (bs == press) {

		boost::shared_ptr<AutomationControl> ac = _fader->control ();

		_fader->set_in_use (true);
		_fader->start_touch (_surface->mcp().transport_frame());

		if (ac) {
			queue_parameter_display ((AutomationType) ac->parameter().type(), ac->get_value());
		}

	} else {

		_fader->set_in_use (false);
		_fader->stop_touch (_surface->mcp().transport_frame(), true);

	}
}


void
Strip::handle_button (Button& button, ButtonState bs)
{
	boost::shared_ptr<AutomationControl> control;

	if (bs == press) {
		button.set_in_use (true);
	} else {
		button.set_in_use (false);
	}

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("strip %1 handling button %2 press ? %3\n", _index, button.bid(), (bs == press)));

	switch (button.bid()) {
	case Button::Select:
		select_event (button, bs);
		break;

	case Button::VSelect:
		vselect_event (button, bs);
		break;

	case Button::FaderTouch:
		fader_touch_event (button, bs);
		break;

	default:
		if ((control = button.control ())) {
			if (bs == press) {
				DEBUG_TRACE (DEBUG::MackieControl, "add button on press\n");
				_surface->mcp().add_down_button ((AutomationType) control->parameter().type(), _surface->number(), _index);

				float new_value;
				int ms = _surface->mcp().main_modifier_state();

				if (ms & MackieControlProtocol::MODIFIER_SHIFT) {
					/* reset to default/normal value */
					new_value = control->normal();
				} else {
					new_value = control->get_value() ? 0.0 : 1.0;
				}

				/* get all controls that either have their
				 * button down or are within a range of
				 * several down buttons
				 */

				MackieControlProtocol::ControlList controls = _surface->mcp().down_controls ((AutomationType) control->parameter().type());


				DEBUG_TRACE (DEBUG::MackieControl, string_compose ("there are %1 buttons down for control type %2, new value = %3\n",
									    controls.size(), control->parameter().type(), new_value));

				/* apply change */

				for (MackieControlProtocol::ControlList::iterator c = controls.begin(); c != controls.end(); ++c) {
					(*c)->set_value (new_value, Controllable::NoGroup);
				}

			} else {
				DEBUG_TRACE (DEBUG::MackieControl, "remove button on release\n");
				_surface->mcp().remove_down_button ((AutomationType) control->parameter().type(), _surface->number(), _index);
			}
		}
		break;
	}
}

void
Strip::queue_parameter_display (AutomationType type, float val)
{
	RedisplayRequest req;

	req.type = type;
	req.val = val;

	redisplay_requests.write (&req, 1);
}

void
Strip::do_parameter_display (AutomationType type, float val)
{
	bool screen_hold = false;
	char buf[16];

	switch (type) {
	case GainAutomation:
		if (val == 0.0) {
			_surface->write (display (1, " -inf "));
		} else {
			float dB = accurate_coefficient_to_dB (val);
			snprintf (buf, sizeof (buf), "%6.1f", dB);
			_surface->write (display (1, buf));
			screen_hold = true;
		}
		break;

	case PanAzimuthAutomation:
		if (Profile->get_mixbus()) {
			snprintf (buf, sizeof (buf), "%2.1f", val);
			_surface->write (display (1, buf));
			screen_hold = true;
		} else {
			if (_route) {
				boost::shared_ptr<Pannable> p = _route->pannable();
				if (p && _route->panner()) {
					string str =_route->panner()->value_as_string (p->pan_azimuth_control);
					_surface->write (display (1, str));
					screen_hold = true;
				}
			}
		}
		break;

	case PanWidthAutomation:
		if (_route) {
			snprintf (buf, sizeof (buf), "%5ld%%", lrintf ((val * 200.0)-100));
			_surface->write (display (1, buf));
			screen_hold = true;
		}
		break;

	case TrimAutomation:
		if (_route) {
			float dB = accurate_coefficient_to_dB (val);
			snprintf (buf, sizeof (buf), "%6.1f", dB);
			_surface->write (display (1, buf));
			screen_hold = true;
		}
		break;

	case PhaseAutomation:
		if (_route) {
			if (_route->phase_control()->get_value() < 0.5) {
				_surface->write (display (1, "Normal"));
			} else {
				_surface->write (display (1, "Invert"));
			}
			screen_hold = true;
		}
		break;

	case SendAutomation:
		if (val == 0.0) {
			_surface->write (display (1, " -inf "));
		} else {
			float dB = accurate_coefficient_to_dB (val);
			snprintf (buf, sizeof (buf), "%6.1f", dB);
			_surface->write (display (1, buf));
			screen_hold = true;
		}
		break;
	case EQGain:
	case EQFrequency:
	case EQQ:
	case EQShape:
	case EQHPF:
	case CompThreshold:
	case CompSpeed:
	case CompMakeup:
	case CompRedux:
		snprintf (buf, sizeof (buf), "%6.1f", val);
		_surface->write (display (1, buf));
		screen_hold = true;
		break;
	case EQEnable:
	case CompEnable:
		if (val >= 0.5) {
			_surface->write (display (1, "on"));
		} else {
			_surface->write (display (1, "off"));
		}
		break;
	case CompMode:
		if (_surface->mcp().subview_route()) {
			_surface->write (display (1, _surface->mcp().subview_route()->comp_mode_name (val)));
		}
		break;
	default:
		break;
	}

	if (screen_hold) {
		block_vpot_mode_display_for (1000);
	}
}

void
Strip::handle_fader_touch (Fader& fader, bool touch_on)
{
	if (touch_on) {
		fader.start_touch (_surface->mcp().transport_frame());
	} else {
		fader.stop_touch (_surface->mcp().transport_frame(), false);
	}
}

void
Strip::handle_fader (Fader& fader, float position)
{
	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("fader to %1\n", position));
	boost::shared_ptr<AutomationControl> ac = fader.control();
	if (!ac) {
		return;
	}

	Controllable::GroupControlDisposition gcd = Controllable::UseGroup;

	if (_surface->mcp().main_modifier_state() & MackieControlProtocol::MODIFIER_SHIFT) {
		gcd = Controllable::NoGroup;
	}

	fader.set_value (position, gcd);

	/* From the Mackie Control MIDI implementation docs:

	   In order to ensure absolute synchronization with the host software,
	   Mackie Control uses a closed-loop servo system for the faders,
	   meaning the faders will always move to their last received position.
	   When a host receives a Fader Position Message, it must then
	   re-transmit that message to the Mackie Control or else the faders
	   will return to their last position.
	*/

	_surface->write (fader.set_position (position));
}

void
Strip::handle_pot (Pot& pot, float delta)
{
	/* Pots only emit events when they move, not when they
	   stop moving. So to get a stop event, we need to use a timeout.
	*/

	boost::shared_ptr<AutomationControl> ac = pot.control();
	if (!ac) {
		return;
	}
	double p = pot.get_value ();
	p += delta;
	// fader and pot should be the same and fader is hard coded 0 -> 1
	p = max (0.0, p);
	p = min (1.0, p);
	pot.set_value (p);
}

void
Strip::periodic (ARDOUR::microseconds_t now)
{
	bool reshow_vpot_mode = false;
	bool reshow_name = false;
	bool good_strip = true;

	if (!_route) {
		// view mode may cover as many as 3 strips
		// needs to be cleared when there are less than 3 routes
		if (_index > 2) {
			return;
		} else {
			good_strip = false;
		}
	}

	if (_block_screen_redisplay_until >= now) {
		if (_surface->mcp().device_info().has_separate_meters()) {
			goto meters;
		}
		/* no drawing here, for now */
		return;

	} else if (_block_screen_redisplay_until) {

		/* timeout reached, reset */

		_block_screen_redisplay_until = 0;
		reshow_vpot_mode = (true && good_strip);
		reshow_name = true;
	}

	if (_block_vpot_mode_redisplay_until >= now) {
		return;
	} else if (_block_vpot_mode_redisplay_until) {

		/* timeout reached, reset */

		_block_vpot_mode_redisplay_until = 0;
		reshow_vpot_mode = (true && good_strip);
	}

	if (reshow_name) {
		show_route_name ();
	}

	if (reshow_vpot_mode) {
		return_to_vpot_mode_display ();
	} else if (good_strip) {
		/* no point doing this if we just switched back to vpot mode
		   display */
		update_automation ();
	}

  meters:
	if (good_strip) {
		update_meter ();
	}
}

void
Strip::redisplay (ARDOUR::microseconds_t now)
{
	RedisplayRequest req;
	bool have_request = false;

	while (redisplay_requests.read (&req, 1) == 1) {
		/* read them all */
		have_request = true;
	}

	if (_block_screen_redisplay_until >= now) {
		return;
	}

	if (have_request) {
		do_parameter_display (req.type, req.val);
	}
}

void
Strip::update_automation ()
{
	if (!_route) {
		return;
	}

	ARDOUR::AutoState state = _route->gain_control()->automation_state();

	if (state == Touch || state == Play) {
		notify_gain_changed (false);
	}

	boost::shared_ptr<AutomationControl> pan_control = _route->pan_azimuth_control ();
	if (pan_control) {
		state = pan_control->automation_state ();
		if (state == Touch || state == Play) {
			notify_panner_azi_changed (false);
		}
	}

	pan_control = _route->pan_width_control ();
	if (pan_control) {
		state = pan_control->automation_state ();
		if (state == Touch || state == Play) {
			notify_panner_width_changed (false);
		}
	}

	if (_route->trim() && route()->trim()->active()) {
		ARDOUR::AutoState trim_state = _route->trim_control()->automation_state();
		if (trim_state == Touch || trim_state == Play) {
			notify_trim_changed (false);
		}
	}
}

void
Strip::update_meter ()
{
	if (_surface->mcp().subview_mode() != MackieControlProtocol::None) {
		return;
	}

	if (_meter && _transport_is_rolling && _metering_active) {
		float dB = const_cast<PeakMeter&> (_route->peak_meter()).meter_level (0, MeterMCP);
		_meter->send_update (*_surface, dB);
	}
}

void
Strip::zero ()
{
	for (Group::Controls::const_iterator it = _controls.begin(); it != _controls.end(); ++it) {
		_surface->write ((*it)->zero ());
	}

	_surface->write (blank_display (0));
	_surface->write (blank_display (1));
}

MidiByteArray
Strip::blank_display (uint32_t line_number)
{
	return display (line_number, string());
}

MidiByteArray
Strip::display (uint32_t line_number, const std::string& line)
{
	assert (line_number <= 1);

	MidiByteArray retval;

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("strip_display index: %1, line %2 = %3\n", _index, line_number, line));

	// sysex header
	retval << _surface->sysex_hdr();

	// code for display
	retval << 0x12;
	// offset (0 to 0x37 first line, 0x38 to 0x6f for second line)
	retval << (_index * 7 + (line_number * 0x38));

	// ascii data to display. @param line is UTF-8
	string ascii = Glib::convert_with_fallback (line, "UTF-8", "ISO-8859-1", "_");
	string::size_type len = ascii.length();
	if (len > 6) {
		ascii = ascii.substr (0, 6);
		len = 6;
	}
	retval << ascii;
	// pad with " " out to 6 chars
	for (int i = len; i < 6; ++i) {
		retval << ' ';
	}

	// column spacer, unless it's the right-hand column
	if (_index < 7) {
		retval << ' ';
	}

	// sysex trailer
	retval << MIDI::eox;

	return retval;
}

void
Strip::lock_controls ()
{
	_controls_locked = true;
}

void
Strip::unlock_controls ()
{
	_controls_locked = false;
}

void
Strip::gui_selection_changed (const ARDOUR::StrongRouteNotificationList& rl)
{
	for (ARDOUR::StrongRouteNotificationList::const_iterator i = rl.begin(); i != rl.end(); ++i) {
		if ((*i) == _route) {
			_surface->write (_select->set_state (on));
			return;
		}
	}

	_surface->write (_select->set_state (off));
}

string
Strip::vpot_mode_string ()
{
	boost::shared_ptr<AutomationControl> ac = _vpot->control();
	if (!ac) {
		return string();
	}

	if (control_by_parameter.find (GainAutomation)->second == _vpot) {
		return "Fader";
	} else if (control_by_parameter.find (TrimAutomation)->second == _vpot) {
		return "Trim";
	} else if (control_by_parameter.find (PhaseAutomation)->second == _vpot) {
		return string_compose ("Phase%1", _route->phase_control()->channel() + 1);
	} else if (control_by_parameter.find (SendAutomation)->second == _vpot) {
		// should be bus name
		boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
		if (p) {
			return p->name();
		}
	} else if (control_by_parameter.find (PanAzimuthAutomation)->second == _vpot) {
		return "Pan";
	} else if (control_by_parameter.find (PanWidthAutomation)->second == _vpot) {
		return "Width";
	} else if (control_by_parameter.find (PanElevationAutomation)->second == _vpot) {
		return "Elev";
	} else if (control_by_parameter.find (PanFrontBackAutomation)->second == _vpot) {
		return "F/Rear";
	} else if (control_by_parameter.find (PanLFEAutomation)->second == _vpot) {
		return "LFE";
	}

	if (_surface->mcp().subview_mode() != MackieControlProtocol::None) {
		return string();
	}

	return "???";
}

 void
Strip::potmode_changed (bool notify)
{
	if (!_route) {
		return;
	}

	// WIP
	int pm = _surface->mcp().pot_mode();
	switch (pm) {
	case MackieControlProtocol::Pan:
		// This needs to set current pan mode (azimuth or width... or whatever)
		DEBUG_TRACE (DEBUG::MackieControl, string_compose ("Assign pot to Pan mode %1\n", enum_2_string (_pan_mode)));
		set_vpot_parameter (_pan_mode);
		break;
	case MackieControlProtocol::Trim:
		DEBUG_TRACE (DEBUG::MackieControl, "Assign pot to Trim mode.\n");
		set_vpot_parameter (_trim_mode);
		break;
	case MackieControlProtocol::Send:
		// _current_send has the number of the send we will show
		DEBUG_TRACE (DEBUG::MackieControl, "Assign pot to Send mode.\n");
		set_vpot_parameter (SendAutomation);
		break;
	}

	if (notify) {
		notify_all ();
	}
}

void
Strip::block_screen_display_for (uint32_t msecs)
{
	_block_screen_redisplay_until = ARDOUR::get_microseconds() + (msecs * 1000);
}

void
Strip::block_vpot_mode_display_for (uint32_t msecs)
{
	_block_vpot_mode_redisplay_until = ARDOUR::get_microseconds() + (msecs * 1000);
}

void
Strip::return_to_vpot_mode_display ()
{
	/* returns the second line of the two-line per-strip display
	   back the mode where it shows what the VPot controls.
	*/

	if (_surface->mcp().subview_mode() != MackieControlProtocol::None) {
		/* do nothing - second line shows value of current subview parameter */
		return;
	} else if (_route) {
		_surface->write (display (1, vpot_mode_string()));
	} else {
		_surface->write (blank_display (1));
	}
}

void
Strip::next_pot_mode ()
{
	vector<AutomationType>::iterator i;

	if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
		/* do not change vpot mode while in flipped mode */
		DEBUG_TRACE (DEBUG::MackieControl, "not stepping pot mode - in flip mode\n");
		_surface->write (display (1, "Flip"));
		block_vpot_mode_display_for (1000);
		return;
	}


	boost::shared_ptr<AutomationControl> ac = _vpot->control();

	if (!ac) {
		return;
	}


	if (_surface->mcp().pot_mode() == MackieControlProtocol::Pan) {

		if (possible_pot_parameters.empty() || (possible_pot_parameters.size() == 1 && possible_pot_parameters.front() == ac->parameter().type())) {
			return;
		}

		for (i = possible_pot_parameters.begin(); i != possible_pot_parameters.end(); ++i) {
			if ((*i) == ac->parameter().type()) {
				break;
			}
		}

		/* move to the next mode in the list, or back to the start (which will
		also happen if the current mode is not in the current pot mode list)
		*/

		if (i != possible_pot_parameters.end()) {
			++i;
		}

		if (i == possible_pot_parameters.end()) {
			i = possible_pot_parameters.begin();
		}

		set_vpot_parameter (*i);
	} else if (_surface->mcp().pot_mode() == MackieControlProtocol::Trim) {
		if (possible_trim_parameters.empty() || (possible_trim_parameters.size() == 1 && possible_trim_parameters.front() == ac->parameter().type())) {
			return;
		}

		for (i = possible_trim_parameters.begin(); i != possible_trim_parameters.end(); ++i) {
			if ((*i) == ac->parameter().type()) {
				break;
			}
		}
		if ((*i) == PhaseAutomation && _route->phase_invert().size() > 1) {
			// There are more than one channel of phase
			if ((_route->phase_control()->channel() + 1) < _route->phase_invert().size()) {
				_route->phase_control()->set_channel(_route->phase_control()->channel() + 1);
				set_vpot_parameter (*i);
				return;
			} else {
				_route->phase_control()->set_channel(0);
			}
		}
		/* move to the next mode in the list, or back to the start (which will
		also happen if the current mode is not in the current pot mode list)
		*/

		if (i != possible_trim_parameters.end()) {
			++i;
		}

		if (i == possible_trim_parameters.end()) {
			i = possible_trim_parameters.begin();
		}
		set_vpot_parameter (*i);
	} else if (_surface->mcp().pot_mode() == MackieControlProtocol::Send) {
		boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
		if (!p) {
			return;
		}
		p = _route->nth_send (_current_send + 1);
		if (p) {
			_current_send++;
			if (p->name() == "Monitor 1") { // skip monitor
				p = _route->nth_send (_current_send + 1);
				if (p) {
					_current_send++;
				} else {
					_current_send = 0;
				}
			}
		} else {
			_current_send = 0;
		}
		set_vpot_parameter (SendAutomation);
	}
}

void
Strip::subview_mode_changed ()
{
	boost::shared_ptr<Route> r = _surface->mcp().subview_route();

	subview_connections.drop_connections ();

	switch (_surface->mcp().subview_mode()) {
	case MackieControlProtocol::None:
		set_vpot_parameter (vpot_parameter);
		notify_metering_state_changed ();
		eq_band = -1;
		break;

	case MackieControlProtocol::EQ:
		if (r) {
			setup_eq_vpot (r);
		} else {
			/* leave it as it was */
		}
		break;

	case MackieControlProtocol::Dynamics:
		if (r) {
			setup_dyn_vpot (r);
		} else {
			/* leave it as it was */
		}
		break;
	}
}

void
Strip::setup_dyn_vpot (boost::shared_ptr<Route> r)
{
	if (!r) {
		return;
	}

	boost::shared_ptr<AutomationControl> tc = r->comp_threshold_controllable ();
	boost::shared_ptr<AutomationControl> sc = r->comp_speed_controllable ();
	boost::shared_ptr<AutomationControl> mc = r->comp_mode_controllable ();
	boost::shared_ptr<AutomationControl> kc = r->comp_makeup_controllable ();
	boost::shared_ptr<AutomationControl> rc = r->comp_redux_controllable ();
	boost::shared_ptr<AutomationControl> ec = r->comp_enable_controllable ();

	uint32_t pos = _surface->mcp().global_index (*this);

	/* we will control the pos-th available parameter, from the list in the
	 * order shown above.
	 */

	vector<boost::shared_ptr<AutomationControl> > available;
	vector<AutomationType> params;

	if (tc) { available.push_back (tc); params.push_back (CompThreshold); }
	if (sc) { available.push_back (sc); params.push_back (CompSpeed); }
	if (mc) { available.push_back (mc); params.push_back (CompMode); }
	if (kc) { available.push_back (kc); params.push_back (CompMakeup); }
	if (rc) { available.push_back (rc); params.push_back (CompRedux); }
	if (ec) { available.push_back (ec); params.push_back (CompEnable); }

	if (pos >= available.size()) {
		/* this knob is not needed to control the available parameters */
		_vpot->set_control (boost::shared_ptr<AutomationControl>());
		_surface->write (display (0, string()));
		_surface->write (display (1, string()));
		return;
	}

	boost::shared_ptr<AutomationControl> pc;
	AutomationType param;

	pc = available[pos];
	param = params[pos];

	pc->Changed.connect (subview_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_dyn_change, this, param, false, true), ui_context());
	_vpot->set_control (pc);

	string pot_id;

	switch (param) {
	case CompThreshold:
		pot_id = "Thresh";
		break;
	case CompSpeed:
		if (mc) {
			pot_id = r->comp_speed_name (mc->get_value());
		} else {
			pot_id = "Speed";
		}
		break;
	case CompMode:
		pot_id = "Mode";
		break;
	case CompMakeup:
		pot_id = "Makeup";
		break;
	case CompRedux:
		pot_id = "Redux";
		break;
	case CompEnable:
		pot_id = "on/off";
		break;
	default:
		break;
	}

	if (!pot_id.empty()) {
		_surface->write (display (0, pot_id));
	}

	notify_dyn_change (param, true, false);
}

void
Strip::setup_eq_vpot (boost::shared_ptr<Route> r)
{
	uint32_t bands = r->eq_band_cnt ();

	if (bands == 0) {
		/* should never get here */
		return;
	}

	/* figure out how many params per band are available */

	boost::shared_ptr<AutomationControl> pc;
	uint32_t params_per_band = 0;

	if ((pc = r->eq_gain_controllable (0))) {
		params_per_band += 1;
	}
	if ((pc = r->eq_freq_controllable (0))) {
		params_per_band += 1;
	}
	if ((pc = r->eq_q_controllable (0))) {
		params_per_band += 1;
	}
	if ((pc = r->eq_shape_controllable (0))) {
		params_per_band += 1;
	}

	/* pick the one for this strip, based on its global position across
	 * all surfaces
	 */

	pc.reset ();

	const uint32_t total_band_parameters = bands * params_per_band;
	const uint32_t global_pos = _surface->mcp().global_index (*this);
	AutomationType param = NullAutomation;
	string band_name;

	eq_band = -1;

	if (global_pos < total_band_parameters) {

		/* show a parameter for an EQ band */

		const uint32_t parameter = global_pos % params_per_band;
		eq_band = global_pos / params_per_band;
		band_name = r->eq_band_name (eq_band);

		switch (parameter) {
		case 0:
			pc = r->eq_gain_controllable (eq_band);
			param = EQGain;
			break;
		case 1:
			pc = r->eq_freq_controllable (eq_band);
			param = EQFrequency;
			break;
		case 2:
			pc = r->eq_q_controllable (eq_band);
			param = EQQ;
			break;
		case 3:
			pc = r->eq_shape_controllable (eq_band);
			param = EQShape;
			break;
		}

	} else {

		/* show a non-band parameter (HPF or enable)
		 */

		uint32_t parameter = global_pos - total_band_parameters;

		switch (parameter) {
		case 0: /* first control after band parameters */
			pc = r->eq_hpf_controllable();
			param = EQHPF;
			break;
		case 1: /* second control after band parameters */
			pc = r->eq_enable_controllable();
			param = EQEnable;
			break;
		default:
			/* nothing to control */
			_vpot->set_control (boost::shared_ptr<AutomationControl>());
			_surface->write (display (0, string()));
			_surface->write (display (1, string()));
			/* done */
			return;
			break;
		}

	}

	if (pc) {
		pc->Changed.connect (subview_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_eq_change, this, param, eq_band, false), ui_context());
		_vpot->set_control (pc);

		string pot_id;

		switch (param) {
		case EQGain:
			pot_id = band_name + "Gain";
			break;
		case EQFrequency:
			pot_id = band_name + "Freq";
			break;
		case EQQ:
			pot_id = band_name + " Q";
			break;
		case EQShape:
			pot_id = band_name + " Shp";
			break;
		case EQHPF:
			pot_id = "HPFreq";
			break;
		case EQEnable:
			pot_id = "on/off";
			break;
		default:
			break;
		}

		if (!pot_id.empty()) {
			_surface->write (display (0, pot_id));
		}

		notify_eq_change (param, eq_band, true);
	}
}

void
Strip::set_vpot_parameter (AutomationType p)
{
	if (!_route || (p == NullAutomation)) {
		control_by_parameter[vpot_parameter] = 0;
		vpot_parameter = NullAutomation;
		_vpot->set_control (boost::shared_ptr<AutomationControl>());
		_surface->write (display (1, string()));
		return;
	}

	boost::shared_ptr<AutomationControl> pan_control;

	DEBUG_TRACE (DEBUG::MackieControl, string_compose ("switch to vpot mode %1\n", p));

	reset_saved_values ();

	/* unset any mapping between the vpot and any existing parameters */

	for (ControlParameterMap::iterator i = control_by_parameter.begin(); i != control_by_parameter.end(); ++i) {

		if (i != control_by_parameter.end() && i->second == _vpot) {
			i->second = 0;
		}
	}

	switch (p) {
	case PanAzimuthAutomation:
		if ((pan_control = _route->pan_azimuth_control ())) {
			if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
				_pan_mode = PanAzimuthAutomation;
				if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
					/* gain to vpot, pan azi to fader */
					_vpot->set_control (_route->gain_control());
					vpot_parameter = GainAutomation;
					control_by_parameter[GainAutomation] = _vpot;
					_fader->set_control (pan_control);
					control_by_parameter[PanAzimuthAutomation] = _fader;
				} else {
					_fader->set_control (boost::shared_ptr<AutomationControl>());
					control_by_parameter[PanAzimuthAutomation] = 0;
				}
			} else {
				/* gain to fader, pan azi to vpot */
				vpot_parameter = PanAzimuthAutomation;
				_fader->set_control (_route->gain_control());
				control_by_parameter[GainAutomation] = _fader;
				_vpot->set_control (pan_control);
				control_by_parameter[PanAzimuthAutomation] = _vpot;
			}
		} else {
			_vpot->set_control (boost::shared_ptr<AutomationControl>());
			control_by_parameter[PanAzimuthAutomation] = 0;
		}
		break;

	case PanWidthAutomation:
		if ((pan_control = _route->pan_width_control ())) {
			if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
				_pan_mode = PanWidthAutomation;
				if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
					/* gain to vpot, pan width to fader */
					_vpot->set_control (_route->gain_control());
					vpot_parameter = GainAutomation;
					control_by_parameter[GainAutomation] = _vpot;
					_fader->set_control (pan_control);
					control_by_parameter[PanWidthAutomation] = _fader;
				} else {
					_fader->set_control (boost::shared_ptr<AutomationControl>());
					control_by_parameter[PanWidthAutomation] = 0;
				}
			} else {
				/* gain to fader, pan width to vpot */
				vpot_parameter = PanWidthAutomation;
				_fader->set_control (_route->gain_control());
				control_by_parameter[GainAutomation] = _fader;
				_vpot->set_control (pan_control);
				control_by_parameter[PanWidthAutomation] = _vpot;
			}
		} else {
			_vpot->set_control (boost::shared_ptr<AutomationControl>());
			control_by_parameter[PanWidthAutomation] = 0;
		}
		break;

	case PanElevationAutomation:
		break;
	case PanFrontBackAutomation:
		break;
	case PanLFEAutomation:
		break;
	case TrimAutomation:
		_trim_mode = TrimAutomation;
		vpot_parameter = TrimAutomation;
		if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
			/* gain to vpot, trim to fader */
			_vpot->set_control (_route->gain_control());
			control_by_parameter[GainAutomation] = _vpot;
			if (_route->trim() && route()->trim()->active()) {
				_fader->set_control (_route->trim_control());
				control_by_parameter[TrimAutomation] = _fader;
			} else {
				_fader->set_control (boost::shared_ptr<AutomationControl>());
				control_by_parameter[TrimAutomation] = 0;
			}
		} else {
			/* gain to fader, trim to vpot */
			_fader->set_control (_route->gain_control());
			control_by_parameter[GainAutomation] = _fader;
			if (_route->trim() && route()->trim()->active()) {
				_vpot->set_control (_route->trim_control());
				control_by_parameter[TrimAutomation] = _vpot;
			} else {
				_vpot->set_control (boost::shared_ptr<AutomationControl>());
				control_by_parameter[TrimAutomation] = 0;
			}
		}
		break;
	case PhaseAutomation:
		_trim_mode = PhaseAutomation;
		vpot_parameter = PhaseAutomation;
		if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
			/* gain to vpot, phase to fader */
			_vpot->set_control (_route->gain_control());
			control_by_parameter[GainAutomation] = _vpot;
			if (_route->phase_invert().size()) {
				_fader->set_control (_route->phase_control());
				control_by_parameter[PhaseAutomation] = _fader;
			} else {
				_fader->set_control (boost::shared_ptr<AutomationControl>());
				control_by_parameter[PhaseAutomation] = 0;
			}
		} else {
			/* gain to fader, phase to vpot */
			_fader->set_control (_route->gain_control());
			control_by_parameter[GainAutomation] = _fader;
			if (_route->phase_invert().size()) {
				_vpot->set_control (_route->phase_control());
				control_by_parameter[PhaseAutomation] = _vpot;
			} else {
				_vpot->set_control (boost::shared_ptr<AutomationControl>());
				control_by_parameter[PhaseAutomation] = 0;
			}
		}
		break;
	case SendAutomation:
		if (!Profile->get_mixbus()) {
			if (_surface->mcp().flip_mode() != MackieControlProtocol::Normal) {
				// gain to vpot, send to fader
				_vpot->set_control (_route->gain_control());
				control_by_parameter[GainAutomation] = _vpot;
				// test for send to control
				boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
				if (p && p->name() != "Monitor 1") {
					boost::shared_ptr<Send> s =  boost::dynamic_pointer_cast<Send>(p);
					boost::shared_ptr<Amp> a = s->amp();
					_fader->set_control (a->gain_control());
					// connect to signal
					send_connections.drop_connections ();
					a->gain_control()->Changed.connect(send_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_processor_changed, this, false), ui_context());
					control_by_parameter[SendAutomation] = _fader;
				} else {
					_fader->set_control (boost::shared_ptr<AutomationControl>());
					control_by_parameter[SendAutomation] = 0;
				}
			} else {
				// gain to fader, send to vpot
				_fader->set_control (_route->gain_control());
				control_by_parameter[GainAutomation] = _fader;
				boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
				if (p && p->name() != "Monitor 1") {
					boost::shared_ptr<Send> s =  boost::dynamic_pointer_cast<Send>(p);
					boost::shared_ptr<Amp> a = s->amp();
					_vpot->set_control (a->gain_control());
					// connect to signal
					send_connections.drop_connections ();
					a->gain_control()->Changed.connect(send_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_processor_changed, this, false), ui_context());
					control_by_parameter[SendAutomation] = _vpot;
				} else {
					// gain to fader, send to vpot
					_fader->set_control (_route->gain_control());
					control_by_parameter[GainAutomation] = _fader;
					boost::shared_ptr<Processor> p = _route->nth_send (_current_send);
					if (p && p->name() != "Monitor 1") {
						boost::shared_ptr<Send> s =  boost::dynamic_pointer_cast<Send>(p);
						boost::shared_ptr<Amp> a = s->amp();
						_vpot->set_control (a->gain_control());
						// connect to signal
						send_connections.drop_connections ();
						a->gain_control()->Changed.connect(send_connections, MISSING_INVALIDATOR, boost::bind (&Strip::notify_processor_changed, this, false), ui_context());
						control_by_parameter[SendAutomation] = _vpot;
					} else {
						_vpot->set_control (boost::shared_ptr<AutomationControl>());
						control_by_parameter[SendAutomation] = 0;
					}
				}
			}
		}
		break;
	default:
		DEBUG_TRACE (DEBUG::MackieControl, string_compose ("vpot mode %1 not known.\n", p));
		break;

	}

	_surface->write (display (1, vpot_mode_string()));
}

bool
Strip::is_midi_track () const
{
	return boost::dynamic_pointer_cast<MidiTrack>(_route) != 0;
}

void
Strip::reset_saved_values ()
{
	_last_pan_azi_position_written = -1.0;
	_last_pan_width_position_written = -1.0;
	_last_gain_position_written = -1.0;
	_last_trim_position_written = -1.0;

}

void
Strip::notify_metering_state_changed()
{
	if (_surface->mcp().subview_mode() != MackieControlProtocol::None) {
		return;
	}

	if (!_route || !_meter) {
		return;
	}

	bool transport_is_rolling = (_surface->mcp().get_transport_speed () != 0.0f);
	bool metering_active = _surface->mcp().metering_active ();

	if ((_transport_is_rolling == transport_is_rolling) && (_metering_active == metering_active)) {
		return;
	}

	_meter->notify_metering_state_changed (*_surface, transport_is_rolling, metering_active);

	if (!transport_is_rolling || !metering_active) {
		notify_property_changed (PBD::PropertyChange (ARDOUR::Properties::name));
		notify_panner_azi_changed (true);
	}

	_transport_is_rolling = transport_is_rolling;
	_metering_active = metering_active;
}
