/*
    Copyright (C) 2008 Hans Baier

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

#include "pbd/compose.h"
#include "pbd/stacktrace.h"

#include "midi++/port.h"
#include "midi++/manager.h"

#include "evoral/midi_events.h"

#include "ardour/ticker.h"
#include "ardour/session.h"
#include "ardour/tempo.h"
#include "ardour/debug.h"

using namespace ARDOUR;

MidiClockTicker::MidiClockTicker ()
	: _midi_port (0)
	, _ppqn (24)
	, _last_tick (0.0)
{
}

void MidiClockTicker::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);
	
	 if (_session) {
		 _session->TransportStateChange.connect_same_thread (_session_connections, boost::bind (&MidiClockTicker::transport_state_changed, this));
		 _session->PositionChanged.connect_same_thread (_session_connections, boost::bind (&MidiClockTicker::position_changed, this, _1));
		 _session->TransportLooped.connect_same_thread (_session_connections, boost::bind (&MidiClockTicker::transport_looped, this));
		 update_midi_clock_port();
	 }
}

void
MidiClockTicker::session_going_away ()
{
	SessionHandlePtr::session_going_away();
	_midi_port = 0;
}

void MidiClockTicker::update_midi_clock_port()
{
	_midi_port = MIDI::Manager::instance()->midi_clock_output_port();
}

void MidiClockTicker::transport_state_changed()
{
	if (_session->exporting()) {
		/* no midi clock during export, for now */
		return;
	}

	float      speed    = _session->transport_speed();
	framepos_t position = _session->transport_frame();

	DEBUG_TRACE (PBD::DEBUG::MidiClock,
		     string_compose ("Transport state change @ %4, speed: %1 position: %2 play loop: %3\n", speed, position, _session->get_play_loop(), position)
		);

	if (speed == 1.0f) {
		_last_tick = position;

		if (!Config->get_send_midi_clock())
			return;

		if (_session->get_play_loop()) {
			assert(_session->locations()->auto_loop_location());
			if (position == _session->locations()->auto_loop_location()->start()) {
				send_start_event(0);
			} else {
				send_continue_event(0);
			}
		} else if (position == 0) {
			send_start_event(0);
		} else {
			send_continue_event(0);
		}

		send_midi_clock_event(0);

	} else if (speed == 0.0f) {
		if (!Config->get_send_midi_clock())
			return;

		send_stop_event(0);
	}

	tick (position);
}

void MidiClockTicker::position_changed (framepos_t position)
{
	DEBUG_TRACE (PBD::DEBUG::MidiClock, string_compose ("Position change: %1\n", position));

	_last_tick = position;
}

void MidiClockTicker::transport_looped()
{
	Location* loop_location = _session->locations()->auto_loop_location();
	assert(loop_location);

	DEBUG_TRACE (PBD::DEBUG::MidiClock,
		     string_compose ("Transport looped, position: %1, loop start: %2, loop end: %3, play loop: %4\n",
				     _session->transport_frame(), loop_location->start(), loop_location->end(), _session->get_play_loop())
		);

	// adjust _last_tick, so that the next MIDI clock message is sent
	// in due time (and the tick interval is still constant)
	framecnt_t elapsed_since_last_tick = loop_location->end() - _last_tick;
	_last_tick = loop_location->start() - elapsed_since_last_tick;
}

void MidiClockTicker::tick (const framepos_t& transport_frames)
{
	if (!Config->get_send_midi_clock() || _session == 0 || _session->transport_speed() != 1.0f || _midi_port == 0)
		return;

	while (true) {
		double next_tick = _last_tick + one_ppqn_in_frames(transport_frames);
		frameoffset_t next_tick_offset = llrint (next_tick) - transport_frames;

		DEBUG_TRACE (PBD::DEBUG::MidiClock,
			     string_compose ("Transport: %1, last tick time: %2, next tick time: %3, offset: %4, cycle length: %5\n",
					     transport_frames, _last_tick, next_tick, next_tick_offset, _midi_port->nframes_this_cycle()
				     )
			);

		if (next_tick_offset >= _midi_port->nframes_this_cycle()) {
			break;
		}

		send_midi_clock_event (next_tick_offset);
		_last_tick = next_tick;
	}
}

double MidiClockTicker::one_ppqn_in_frames (framepos_t transport_position)
{
	const Tempo& current_tempo = _session->tempo_map().tempo_at(transport_position);
	double frames_per_beat = current_tempo.frames_per_beat(_session->nominal_frame_rate());

	double quarter_notes_per_beat = 4.0 / current_tempo.note_type();
	double frames_per_quarter_note = frames_per_beat / quarter_notes_per_beat;

	return frames_per_quarter_note / double (_ppqn);
}

void MidiClockTicker::send_midi_clock_event (pframes_t offset)
{
	if (!_midi_port) {
		return;
	}

	DEBUG_TRACE (PBD::DEBUG::MidiClock, string_compose ("Tick with offset %1\n", offset));

	static uint8_t _midi_clock_tick[1] = { MIDI_CMD_COMMON_CLOCK };
	_midi_port->write (_midi_clock_tick, 1, offset);
}

void MidiClockTicker::send_start_event (pframes_t offset)
{
	if (!_midi_port) {
		return;
	}

	static uint8_t _midi_clock_tick[1] = { MIDI_CMD_COMMON_START };
	_midi_port->write (_midi_clock_tick, 1, offset);
}

void MidiClockTicker::send_continue_event (pframes_t offset)
{
	if (!_midi_port) {
		return;
	}

	static uint8_t _midi_clock_tick[1] = { MIDI_CMD_COMMON_CONTINUE };
	_midi_port->write (_midi_clock_tick, 1, offset);
}

void MidiClockTicker::send_stop_event (pframes_t offset)
{
	if (!_midi_port) {
		return;
	}

	static uint8_t _midi_clock_tick[1] = { MIDI_CMD_COMMON_STOP };
	_midi_port->write (_midi_clock_tick, 1, offset);
}



