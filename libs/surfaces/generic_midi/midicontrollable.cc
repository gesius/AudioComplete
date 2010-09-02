/*
    Copyright (C) 1998-2006 Paul Davis

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

#define __STDC_FORMAT_MACROS 1
#include <stdint.h>
#include <cmath>
#include <climits>
#include <iostream>

#include "pbd/error.h"
#include "pbd/controllable_descriptor.h"
#include "pbd/xml++.h"

#include "midi++/port.h"
#include "midi++/channel.h"

#include "ardour/automation_control.h"
#include "ardour/utils.h"

#include "midicontrollable.h"

using namespace std;
using namespace MIDI;
using namespace PBD;
using namespace ARDOUR;

MIDIControllable::MIDIControllable (Port& p, bool m)
	: controllable (0)
	, _descriptor (0)
	, _port (p)
	, _momentary (m)
{
	_learned = false; /* from URI */
	setting = false;
	last_value = 0; // got a better idea ?
	control_type = none;
	_control_description = "MIDI Control: none";
	control_additional = (byte) -1;
	feedback = true; // for now
}

MIDIControllable::MIDIControllable (Port& p, Controllable& c, bool m)
	: controllable (&c)
	, _descriptor (0)
	, _port (p)
	, _momentary (m)
{
	_learned = true; /* from controllable */
	setting = false;
	last_value = 0; // got a better idea ?
	control_type = none;
	_control_description = "MIDI Control: none";
	control_additional = (byte) -1;
	feedback = true; // for now
}

MIDIControllable::~MIDIControllable ()
{
	drop_external_control ();
}

int
MIDIControllable::init (const std::string& s)
{
	_current_uri = s;
	delete _descriptor;
	_descriptor = new ControllableDescriptor;
	return _descriptor->set (s);
}

void
MIDIControllable::midi_forget ()
{
	/* stop listening for incoming messages, but retain
	   our existing event + type information.
	*/

	midi_sense_connection[0].disconnect ();
	midi_sense_connection[1].disconnect ();
	midi_learn_connection.disconnect ();
}

void
MIDIControllable::drop_external_control ()
{
	midi_forget ();
	control_type = none;
	control_additional = (byte) -1;
}

void
MIDIControllable::set_controllable (Controllable* c)
{
	controllable = c;
}

void
MIDIControllable::midi_rebind (channel_t c)
{
	if (c >= 0) {
		bind_midi (c, control_type, control_additional);
	} else {
		midi_forget ();
	}
}

void
MIDIControllable::learn_about_external_control ()
{
	drop_external_control ();
	_port.parser()->any.connect_same_thread (midi_learn_connection, boost::bind (&MIDIControllable::midi_receiver, this, _1, _2, _3));
}

void
MIDIControllable::stop_learning ()
{
	midi_learn_connection.disconnect ();
}

float
MIDIControllable::control_to_midi (float val)
{
	const float midi_range = 127.0f; // TODO: NRPN etc.

        if (controllable->is_gain_like()) {
                return gain_to_slider_position (val/midi_range);
        }

	float control_min = controllable->lower ();
	float control_max = controllable->upper ();
	const float control_range = control_max - control_min;

	return (val - control_min) / control_range * midi_range;
}

float
MIDIControllable::midi_to_control(float val)
{
	const float midi_range = 127.0f; // TODO: NRPN etc.

        if (controllable->is_gain_like()) {
                return slider_position_to_gain (val/midi_range);
        }

	float control_min = controllable->lower ();
	float control_max = controllable->upper ();
	const float control_range = control_max - control_min;

	return  val / midi_range * control_range + control_min;
}

void
MIDIControllable::midi_sense_note_on (Parser &p, EventTwoBytes *tb)
{
	midi_sense_note (p, tb, true);
}

void
MIDIControllable::midi_sense_note_off (Parser &p, EventTwoBytes *tb)
{
	midi_sense_note (p, tb, false);
}

void
MIDIControllable::midi_sense_note (Parser &, EventTwoBytes *msg, bool /*is_on*/)
{
	if (!controllable) { 
		return;
	}


	if (!controllable->is_toggle()) {
		controllable->set_value (msg->note_number/127.0);
	} else {

		if (control_additional == msg->note_number) {
			controllable->set_value (controllable->get_value() > 0.5f ? 0.0f : 1.0f);
		}
	}

	last_value = (MIDI::byte) (controllable->get_value() * 127.0); // to prevent feedback fights
}

void
MIDIControllable::midi_sense_controller (Parser &, EventTwoBytes *msg)
{
	if (!controllable) { 
		return;
	}

	if (controllable->touching()) {
		return; // to prevent feedback fights when e.g. dragging a UI slider
	}

	if (control_additional == msg->controller_number) {

		if (!controllable->is_toggle()) {
			controllable->set_value (midi_to_control (msg->value));
		} else {
			if (msg->value > 64.0f) {
				controllable->set_value (1);
			} else {
				controllable->set_value (0);
			}
		}

		last_value = (MIDI::byte) (control_to_midi(controllable->get_value())); // to prevent feedback fights
	}
}

void
MIDIControllable::midi_sense_program_change (Parser &, byte msg)
{
	if (!controllable) { 
		return;
	}

	if (!controllable->is_toggle()) {
		controllable->set_value (msg/127.0);
	} else {
		controllable->set_value (controllable->get_value() > 0.5f ? 0.0f : 1.0f);
	}

	last_value = (MIDI::byte) (controllable->get_value() * 127.0); // to prevent feedback fights
}

void
MIDIControllable::midi_sense_pitchbend (Parser &, pitchbend_t pb)
{
	if (!controllable) { 
		return;
	}

	if (!controllable->is_toggle()) {
		/* XXX gack - get rid of assumption about typeof pitchbend_t */
		controllable->set_value ((pb/(float) SHRT_MAX));
	} else {
		controllable->set_value (controllable->get_value() > 0.5f ? 0.0f : 1.0f);
	}

	last_value = (MIDI::byte) (controllable->get_value() * 127.0); // to prevent feedback fights
}

void
MIDIControllable::midi_receiver (Parser &, byte *msg, size_t /*len*/)
{
	/* we only respond to channel messages */

	if ((msg[0] & 0xF0) < 0x80 || (msg[0] & 0xF0) > 0xE0) {
		return;
	}

	/* if the our port doesn't do input anymore, forget it ... */

	if (!_port.parser()) {
		return;
	}

	bind_midi ((channel_t) (msg[0] & 0xf), eventType (msg[0] & 0xF0), msg[1]);

	controllable->LearningFinished ();
}

void
MIDIControllable::bind_midi (channel_t chn, eventType ev, MIDI::byte additional)
{
	char buf[64];

	drop_external_control ();

	control_type = ev;
	control_channel = chn;
	control_additional = additional;

	if (_port.parser() == 0) {
		return;
	}

	Parser& p = *_port.parser();

	int chn_i = chn;
	switch (ev) {
	case MIDI::off:
		p.channel_note_off[chn_i].connect_same_thread (midi_sense_connection[0], boost::bind (&MIDIControllable::midi_sense_note_off, this, _1, _2));

		/* if this is a togglee, connect to noteOn as well,
		   and we'll toggle back and forth between the two.
		*/

		if (_momentary) {
			p.channel_note_on[chn_i].connect_same_thread (midi_sense_connection[1], boost::bind (&MIDIControllable::midi_sense_note_on, this, _1, _2));
		} 

		_control_description = "MIDI control: NoteOff";
		break;

	case MIDI::on:
		p.channel_note_on[chn_i].connect_same_thread (midi_sense_connection[0], boost::bind (&MIDIControllable::midi_sense_note_on, this, _1, _2));
		if (_momentary) {
			p.channel_note_off[chn_i].connect_same_thread (midi_sense_connection[1], boost::bind (&MIDIControllable::midi_sense_note_off, this, _1, _2));
		}
		_control_description = "MIDI control: NoteOn";
		break;
		
	case MIDI::controller:
		p.channel_controller[chn_i].connect_same_thread (midi_sense_connection[0], boost::bind (&MIDIControllable::midi_sense_controller, this, _1, _2));
		snprintf (buf, sizeof (buf), "MIDI control: Controller %d", control_additional);
		_control_description = buf;
		break;

	case MIDI::program:
		p.channel_program_change[chn_i].connect_same_thread (midi_sense_connection[0], boost::bind (&MIDIControllable::midi_sense_program_change, this, _1, _2));
		_control_description = "MIDI control: ProgramChange";
		break;

	case MIDI::pitchbend:
		p.channel_pitchbend[chn_i].connect_same_thread (midi_sense_connection[0], boost::bind (&MIDIControllable::midi_sense_pitchbend, this, _1, _2));
		_control_description = "MIDI control: Pitchbend";
		break;

	default:
		break;
	}
}

void
MIDIControllable::send_feedback ()
{
	byte msg[3];

	if (!_learned || setting || !feedback || control_type == none) {
		return;
	}

	msg[0] = (control_type & 0xF0) | (control_channel & 0xF);
	msg[1] = control_additional;

	if (controllable->is_gain_like()) {
		msg[2] = (byte) lrintf (gain_to_slider_position (controllable->get_value()) * 127.0f);
	} else {
		msg[2] = (byte) (control_to_midi(controllable->get_value()));
	}

	_port.write (msg, 3, 0);
}

MIDI::byte*
MIDIControllable::write_feedback (MIDI::byte* buf, int32_t& bufsize, bool /*force*/)
{
	if (control_type != none && feedback && bufsize > 2) {

		MIDI::byte gm;

		if (controllable->is_gain_like()) {
			gm = (byte) lrintf (gain_to_slider_position (controllable->get_value()) * 127.0f);
		} else {
			gm = (byte) (control_to_midi(controllable->get_value()));
		}

		if (gm != last_value) {
			*buf++ = (0xF0 & control_type) | (0xF & control_channel);
			*buf++ = control_additional; /* controller number */
			*buf++ = gm;
			last_value = gm;
			bufsize -= 3;
		}
	}

	return buf;
}

int
MIDIControllable::set_state (const XMLNode& node, int /*version*/)
{
	const XMLProperty* prop;
	int xx;

	if ((prop = node.property ("event")) != 0) {
		sscanf (prop->value().c_str(), "0x%x", &xx);
		control_type = (MIDI::eventType) xx;
	} else {
		return -1;
	}

	if ((prop = node.property ("channel")) != 0) {
		sscanf (prop->value().c_str(), "%d", &xx);
		control_channel = (MIDI::channel_t) xx;
	} else {
		return -1;
	}

	if ((prop = node.property ("additional")) != 0) {
		sscanf (prop->value().c_str(), "0x%x", &xx);
		control_additional = (MIDI::byte) xx;
	} else {
		return -1;
	}

	if ((prop = node.property ("feedback")) != 0) {
		feedback = (prop->value() == "yes");
	} else {
		feedback = true; // default
	}

	bind_midi (control_channel, control_type, control_additional);

	return 0;
}

XMLNode&
MIDIControllable::get_state ()
{
	char buf[32];

	XMLNode* node = new XMLNode ("MIDIControllable");

	if (!_current_uri.empty()) {
		node->add_property ("uri", _current_uri);
	}

	if (controllable) {
		snprintf (buf, sizeof(buf), "0x%x", (int) control_type);
		node->add_property ("event", buf);
		snprintf (buf, sizeof(buf), "%d", (int) control_channel);
		node->add_property ("channel", buf);
		snprintf (buf, sizeof(buf), "0x%x", (int) control_additional);
		node->add_property ("additional", buf);
		node->add_property ("feedback", (feedback ? "yes" : "no"));
	}

	return *node;
}

