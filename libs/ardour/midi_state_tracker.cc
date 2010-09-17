/*
    Copyright (C) 2006-2008 Paul Davis
    Author: Torben Hohn

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <iostream>
#include "ardour/event_type_map.h"
#include "ardour/midi_ring_buffer.h"
#include "ardour/midi_source.h"
#include "ardour/midi_state_tracker.h"

using namespace std;
using namespace ARDOUR;


MidiStateTracker::MidiStateTracker ()
{
	reset ();
}

void
MidiStateTracker::reset ()
{
	memset (_active_notes, 0, sizeof (_active_notes));
	_on = 0;
}

void
MidiStateTracker::track_note_onoffs (const Evoral::MIDIEvent<MidiBuffer::TimeType>& event)
{
	if (event.is_note_on()) {
		add (event.note(), event.channel());
	} else if (event.is_note_off()){
		remove (event.note(), event.channel());
	}
}

void
MidiStateTracker::add (uint8_t note, uint8_t chn)
{
	++_active_notes[note + 128 * chn];
	++_on;
}

void
MidiStateTracker::remove (uint8_t note, uint8_t chn)
{
	switch (_active_notes[note + 128 * chn]) {
	case 0:
		break;
	case 1:
		--_on;
		_active_notes [note + 128 * chn] = 0;
		break;
	default:
		--_active_notes [note + 128 * chn];
				break;

	}
}

void
MidiStateTracker::track (const MidiBuffer::iterator &from, const MidiBuffer::iterator &to, bool& looped)
{
	looped = false;

	for (MidiBuffer::iterator i = from; i != to; ++i) {
		const Evoral::MIDIEvent<MidiBuffer::TimeType> ev(*i, false);
		if (ev.event_type() == LoopEventType) {
			looped = true;
			continue;
		}

		track_note_onoffs (ev);
	}
}

void
MidiStateTracker::resolve_notes (MidiBuffer &dst, framepos_t time)
{
	if (!_on) {
		return;
	}

	for (int channel = 0; channel < 16; ++channel) {
		for (int note = 0; note < 128; ++note) {
			while (_active_notes[note + 128 * channel]) {
				uint8_t buffer[3] = { MIDI_CMD_NOTE_OFF | channel, note, 0 };
				Evoral::MIDIEvent<MidiBuffer::TimeType> noteoff
					(MIDI_CMD_NOTE_OFF, time, 3, buffer, false);
				dst.push_back (noteoff);
				_active_notes[note + 128 * channel]--;
			}
		}
	}
	_on = 0;
}

void
MidiStateTracker::resolve_notes (Evoral::EventSink<nframes_t> &dst, framepos_t time)
{
	uint8_t buf[3];

	if (!_on) {
		return;
	}

	for (int channel = 0; channel < 16; ++channel) {
		for (int note = 0; note < 128; ++note) {
			while (_active_notes[note + 128 * channel]) {
				buf[0] = MIDI_CMD_NOTE_OFF|channel;
				buf[1] = note;
				buf[2] = 0;
				dst.write (time, EventTypeMap::instance().midi_event_type (buf[0]), 3, buf);
				_active_notes[note + 128 * channel]--;
			}
		}
	}
	_on = 0;
}

void
MidiStateTracker::resolve_notes (MidiSource& src, Evoral::MusicalTime time)
{
	if (!_on) {
		return;
	}

        /* NOTE: the src must be locked */

	for (int channel = 0; channel < 16; ++channel) {
		for (int note = 0; note < 128; ++note) {
			while (_active_notes[note + 128 * channel]) {
                                Evoral::MIDIEvent<Evoral::MusicalTime> ev ((MIDI_CMD_NOTE_OFF|channel), time, 3, 0, true);
                                ev.set_type (MIDI_CMD_NOTE_OFF);
                                ev.set_channel (channel);
                                ev.set_note (note);
                                ev.set_velocity (0);
                                src.append_event_unlocked_beats (ev);
				_active_notes[note + 128 * channel]--;
                                /* don't stack events up at the same time
                                 */
                                time += 1.0/128.0;
			}
		}
	}
	_on = 0;
}

void
MidiStateTracker::dump (ostream& o)
{
	o << "******\n";
	for (int c = 0; c < 16; ++c) {
		for (int x = 0; x < 128; ++x) {
			if (_active_notes[c * 128 + x]) {
				o << "Channel " << c+1 << " Note " << x << " is on ("
				  << (int) _active_notes[c*128+x] <<  "times)\n";
			}
		}
	}
	o << "+++++\n";
}
