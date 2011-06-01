/*
    Copyright (C) 2006 Paul Davis

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

#include <cassert>
#include <iostream>

#include "ardour/midi_port.h"
#include "ardour/data_type.h"
#include "ardour/audioengine.h"

using namespace ARDOUR;
using namespace std;

MidiPort::MidiPort (const std::string& name, Flags flags)
	: Port (name, DataType::MIDI, flags)
	, _has_been_mixed_down (false)
	, _resolve_required (false)
{
	_buffer = new MidiBuffer (AudioEngine::instance()->raw_buffer_size (DataType::MIDI));
}

MidiPort::~MidiPort()
{
	delete _buffer;
}

void
MidiPort::cycle_start (pframes_t nframes)
{
	Port::cycle_start (nframes);

	_buffer->clear ();

	assert (_buffer->size () == 0);

	if (sends_output ()) {
		jack_midi_clear_buffer (jack_port_get_buffer (_jack_port, nframes));
	}
}

MidiBuffer &
MidiPort::get_midi_buffer (pframes_t nframes)
{
	if (_has_been_mixed_down) {
		return *_buffer;
	}

	if (receives_input ()) {

		void* jack_buffer = jack_port_get_buffer (_jack_port, nframes);
		const pframes_t event_count = jack_midi_get_event_count (jack_buffer);

		assert (event_count < _buffer->capacity());

		/* suck all relevant MIDI events from the JACK MIDI port buffer
		   into our MidiBuffer
		*/

		for (pframes_t i = 0; i < event_count; ++i) {

			jack_midi_event_t ev;

			jack_midi_event_get (&ev, jack_buffer, i);

			if (ev.buffer[0] == 0xfe) {
				/* throw away active sensing */
				continue;
			}

			/* check that the event is in the acceptable time range */

			if ((ev.time >= (_global_port_buffer_offset + _port_buffer_offset)) &&
			    (ev.time < (_global_port_buffer_offset + _port_buffer_offset + nframes))) {
				_buffer->push_back (ev);
			} else {
				cerr << "Dropping incoming MIDI at time " << ev.time << "; offset="
				     << _global_port_buffer_offset << " limit="
				     << (_global_port_buffer_offset + _port_buffer_offset + nframes) << "\n";
			}
		}

	} else {
		_buffer->silence (nframes);
	}

	if (nframes) {
		_has_been_mixed_down = true;
	}

	return *_buffer;
}


void
MidiPort::cycle_end (pframes_t /*nframes*/)
{
	_has_been_mixed_down = false;
}

void
MidiPort::cycle_split ()
{
	_has_been_mixed_down = false;
}

void
MidiPort::resolve_notes (void* jack_buffer, MidiBuffer::TimeType when)
{
	uint8_t ev[3];

	ev[2] = 0;

	for (uint8_t channel = 0; channel <= 0xF; channel++) {
		ev[0] = (MIDI_CMD_CONTROL | channel);

		/* we need to send all notes off AND turn the
		 * sustain/damper pedal off to handle synths
		 * that prioritize sustain over AllNotesOff
		 */

		ev[1] = MIDI_CTL_SUSTAIN;

		if (jack_midi_event_write (jack_buffer, when, ev, 3) != 0) {
			cerr << "failed to deliver sustain-zero on channel " << channel << " on port " << name() << endl;
		}

		ev[1] = MIDI_CTL_ALL_NOTES_OFF;

		if (jack_midi_event_write (jack_buffer, 0, ev, 3) != 0) {
			cerr << "failed to deliver ALL NOTES OFF on channel " << channel << " on port " << name() << endl;
		}
	}
}

void
MidiPort::flush_buffers (pframes_t nframes, framepos_t time)
{
	if (sends_output ()) {

		void* jack_buffer = jack_port_get_buffer (_jack_port, nframes);

		if (_resolve_required) {
			/* resolve all notes at the start of the buffer */
			resolve_notes (jack_buffer, 0);
			_resolve_required= false;
		}

		for (MidiBuffer::iterator i = _buffer->begin(); i != _buffer->end(); ++i) {

			const Evoral::MIDIEvent<MidiBuffer::TimeType> ev (*i, false);

			// event times are in frames, relative to cycle start

			assert (ev.time() < (nframes + _global_port_buffer_offset + _port_buffer_offset));

			if (ev.event_type() == LoopEventType) {
				resolve_notes (jack_buffer, ev.time());
				continue;
			}

			if (ev.time() >= _global_port_buffer_offset + _port_buffer_offset) {
				if (jack_midi_event_write (jack_buffer, (jack_nframes_t) ev.time(), ev.buffer(), ev.size()) != 0) {
					cerr << "write failed, drop flushed note off on the floor, time "
					     << ev.time() << " > " << _global_port_buffer_offset + _port_buffer_offset << endl;
				}
			} else {
				cerr << "drop flushed event on the floor, time " << ev.time()
				     << " < " << _global_port_buffer_offset + _port_buffer_offset << endl;
			}
		}
	}
}

void
MidiPort::transport_stopped ()
{
	_resolve_required = true;
}

void
MidiPort::realtime_locate ()
{
	_resolve_required = true;
}

void
MidiPort::reset ()
{
	Port::reset ();
	delete _buffer;
	_buffer = new MidiBuffer (AudioEngine::instance()->raw_buffer_size (DataType::MIDI));
}
