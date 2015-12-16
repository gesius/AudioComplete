#ifndef __ardour_mackie_control_protocol_strip_h__
#define __ardour_mackie_control_protocol_strip_h__

#include <string>
#include <iostream>

#include "evoral/Parameter.hpp"

#include "pbd/property_basics.h"
#include "pbd/ringbuffer.h"
#include "pbd/signals.h"

#include "ardour/types.h"
#include "control_protocol/types.h"

#include "control_group.h"
#include "types.h"
#include "midi_byte_array.h"
#include "device_info.h"

namespace ARDOUR {
	class Route;
	class Bundle;
	class ChannelCount;
}

namespace ArdourSurface {

namespace Mackie {

class Control;
class Surface;
class Button;
class Pot;
class Fader;
class Meter;
class SurfacePort;

struct GlobalControlDefinition {
    const char* name;
    int id;
    Control* (*factory)(Surface&, int index, const char* name, Group&);
    const char* group_name;
};

/**
	This is the set of controls that make up a strip.
*/
class Strip : public Group
{
public:
	Strip (Surface&, const std::string & name, int index, const std::map<Button::ID,StripButtonInfo>&);
	~Strip();

	boost::shared_ptr<ARDOUR::Route> route() const { return _route; }

	void add (Control & control);
	int index() const { return _index; } // zero based

	void set_route (boost::shared_ptr<ARDOUR::Route>, bool with_messages = true);

	// call all signal handlers manually
	void notify_all ();

	void handle_button (Button&, ButtonState bs);
	void handle_fader (Fader&, float position);
	void handle_fader_touch (Fader&, bool touch_on);
	void handle_pot (Pot&, float delta);

	void periodic (ARDOUR::microseconds_t now_usecs);
	void redisplay (ARDOUR::microseconds_t now_usecs);

	MidiByteArray display (uint32_t line_number, const std::string&);
	MidiByteArray blank_display (uint32_t line_number);

	void zero ();

	void potmode_changed (bool notify=false);

	void lock_controls ();
	void unlock_controls ();
	bool locked() const { return _controls_locked; }

	void gui_selection_changed (const ARDOUR::StrongRouteNotificationList&);

	void notify_metering_state_changed();

	void block_screen_display_for (uint32_t msecs);
	void block_vpot_mode_display_for (uint32_t msecs);

private:
	Button*  _solo;
	Button*  _recenable;
	Button*  _mute;
	Button*  _select;
	Button*  _vselect;
	Button*  _fader_touch;
	Pot*     _vpot;
	Fader*   _fader;
	Meter*   _meter;
	int      _index;
	Surface* _surface;
	bool     _controls_locked;
	bool     _transport_is_rolling;
	bool     _metering_active;
	uint64_t _block_vpot_mode_redisplay_until;
	uint64_t _block_screen_redisplay_until;
	boost::shared_ptr<ARDOUR::Route> _route;
	PBD::ScopedConnectionList route_connections;
	PBD::ScopedConnectionList send_connections;

	ARDOUR::AutomationType  _pan_mode;
	ARDOUR::AutomationType  _trim_mode;

	float _last_gain_position_written;
	float _last_pan_azi_position_written;
	float _last_pan_width_position_written;
	float _last_trim_position_written;
	uint32_t _current_send;

	void notify_solo_changed ();
	void notify_mute_changed ();
	void notify_record_enable_changed ();
	void notify_gain_changed (bool force_update = true);
	void notify_property_changed (const PBD::PropertyChange&);
	void notify_panner_azi_changed (bool force_update = true);
	void notify_panner_width_changed (bool force_update = true);
	void notify_active_changed ();
	void notify_route_deleted ();
	void notify_trim_changed (bool force_update = true);
	void notify_phase_changed (bool force_update = true);
	void notify_processor_changed (bool force_update = true);
	void update_automation ();
	void update_meter ();
	std::string vpot_mode_string ();

	boost::shared_ptr<ARDOUR::AutomationControl> mb_pan_controllable;
	
	void return_to_vpot_mode_display ();

	struct RedisplayRequest {
		ARDOUR::AutomationType type;
		float val;
	};

	RingBuffer<RedisplayRequest> redisplay_requests;

	void do_parameter_display (ARDOUR::AutomationType, float val);
	void queue_parameter_display (ARDOUR::AutomationType, float val);

	void select_event (Button&, ButtonState);
	void vselect_event (Button&, ButtonState);
	void fader_touch_event (Button&, ButtonState);

	std::vector<Evoral::Parameter> possible_pot_parameters;
	std::vector<Evoral::Parameter> possible_trim_parameters;
	void next_pot_mode ();
	void set_vpot_parameter (Evoral::Parameter);
	void show_route_name ();

	void reset_saved_values ();

	bool is_midi_track () const;

	typedef std::map<Evoral::Parameter,Control*> ControlParameterMap;
	ControlParameterMap control_by_parameter;
};

}
}

#endif /* __ardour_mackie_control_protocol_strip_h__ */
