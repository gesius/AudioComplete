/*
    Copyright (C) 2006-2008 Paul Davis

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

#include "ardour/debug.h"
#include "ardour/midi_ring_buffer.h"
#include "ardour/midi_buffer.h"
#include "ardour/event_type_map.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

/** Read a block of MIDI events from this buffer into a MidiBuffer.
 *
 * Timestamps of events returned are relative to start (i.e. event with stamp 0
 * occurred at start), with offset added.
 */
template<typename T>
size_t
MidiRingBuffer<T>::read(MidiBuffer& dst, framepos_t start, framepos_t end, framecnt_t offset)
{
	if (this->read_space() == 0) {
		return 0;
	}

	T                 ev_time;
	Evoral::EventType ev_type;
	uint32_t          ev_size;

	/* If we see the end of a loop during this read, we must write the events after it
	   to the MidiBuffer with adjusted times.  The situation is as follows:

           session frames----------------------------->
	   
	             |                            |                    |
	        start_of_loop                   start              end_of_loop

	   The MidiDiskstream::read method which will have happened before this checks for
	   loops ending, and helpfully inserts a magic LoopEvent into the ringbuffer.  After this,
	   the MidiDiskstream continues to write events with their proper session frame times,
	   so after the LoopEvent event times will go backwards (ie non-monotonically).

	   Once we hit end_of_loop, we need to fake it to make it look as though the loop has been
	   immediately repeated.  Say that an event E after the end_of_loop in the ringbuffer
	   has time E_t, which is a time in session frames.  Its offset from the start
	   of the loop will be E_t - start_of_loop.  Its `faked' time will therefore be
	   end_of_loop + E_t - start_of_loop.  And so its port-buffer-relative time (for
	   writing to the MidiBuffer) will be end_of_loop + E_t - start_of_loop - start.

	   The subtraction of start is already taken care of, so if we see a LoopEvent, we'll
	   set up loop_offset to equal end_of_loop - start_of_loop, so that given an event
	   time E_t in the ringbuffer we can get the port-buffer-relative time as
	   E_t + offset - start.
	*/

	frameoffset_t loop_offset = 0;

	size_t count = 0;

	while (this->read_space() >= sizeof(T) + sizeof(Evoral::EventType) + sizeof(uint32_t)) {

		this->full_peek(sizeof(T), (uint8_t*)&ev_time);

		if (ev_time + loop_offset >= end) {
			DEBUG_TRACE (DEBUG::MidiDiskstreamIO, string_compose ("MRB event @ %1 past end @ %2\n", ev_time, end));
			break;
		} else if (ev_time + loop_offset < start) {
			DEBUG_TRACE (DEBUG::MidiDiskstreamIO, string_compose ("MRB event @ %1 before start @ %2\n", ev_time, start));
			break;
		} else {
			DEBUG_TRACE (DEBUG::MidiDiskstreamIO, string_compose ("MRB event @ %1 in range %2 .. %3\n", ev_time, start, end));
		}

		bool success = read_prefix(&ev_time, &ev_type, &ev_size);
		if (!success) {
			cerr << "WARNING: error reading event prefix from MIDI ring" << endl;
			continue;
		}

		// This event marks a loop end (i.e. the next event's timestamp will be non-monotonic)
		if (ev_type == LoopEventType) {
			assert (ev_size == sizeof (framepos_t));
			framepos_t loop_start;
			read_contents (ev_size, (uint8_t *) &loop_start);

			loop_offset = ev_time - loop_start;
			continue;
		}

		ev_time += loop_offset;

		uint8_t status;
		success = this->full_peek(sizeof(uint8_t), &status);
		assert(success); // If this failed, buffer is corrupt, all hope is lost

		// Ignore event if it doesn't match channel filter
		if (is_channel_event(status) && get_channel_mode() == FilterChannels) {
			const uint8_t channel = status & 0x0F;
			if (!(get_channel_mask() & (1L << channel))) {
				// cerr << "MRB skipping event due to channel mask" << endl;
				this->skip(ev_size); // Advance read pointer to next event
				continue;
			}
		}

		assert(ev_time >= start);
		
		ev_time -= start;
		ev_time += offset;

		// write the timestamp to address (write_loc - 1)
		uint8_t* write_loc = dst.reserve(ev_time, ev_size);
		if (write_loc == NULL) {
			cerr << "MRB: Unable to reserve space in buffer, event skipped";
			this->skip (ev_size); // Advance read pointer to next event
			continue;
		}

		// write MIDI buffer contents
		success = read_contents (ev_size, write_loc);

#ifndef NDEBUG
                DEBUG_STR_DECL(a);
                DEBUG_STR_APPEND(a, string_compose ("wrote MidiEvent to Buffer (time=%1, start=%2 offset=%3)", ev_time, start, offset));
		for (size_t i=0; i < ev_size; ++i) {
			DEBUG_STR_APPEND(a,hex);
			DEBUG_STR_APPEND(a,"0x");
			DEBUG_STR_APPEND(a,(int)write_loc[i]);
                        DEBUG_STR_APPEND(a,' ');
		}
                DEBUG_STR_APPEND(a,'\n');
                DEBUG_TRACE (DEBUG::MidiDiskstreamIO, DEBUG_STR(a).str());
#endif

		if (success) {
			if (is_channel_event(status) && get_channel_mode() == ForceChannel) {
				write_loc[0] = (write_loc[0] & 0xF0) | (get_channel_mask() & 0x0F);
			}
			++count;
		} else {
			cerr << "WARNING: error reading event contents from MIDI ring" << endl;
		}
	}

	return count;
}
template<typename T>
void
MidiRingBuffer<T>::dump(ostream& str)
{
	size_t rspace;

	if ((rspace = this->read_space()) == 0) {
		str << "MRB::dump: empty\n";
		return;
	}

	T                 ev_time;
	Evoral::EventType ev_type;
	uint32_t          ev_size;
	size_t read_ptr = g_atomic_int_get (&this->_read_ptr);

	str << "Dump @ " << read_ptr << endl;

	while (1) {
		uint8_t* wp;
		uint8_t* data;
		size_t write_ptr;

#define space(r,w) ((w > r) ? (w - r) : ((w - r + this->_size) % this->_size))

		write_ptr  = g_atomic_int_get (&this->_write_ptr);
		if (space (read_ptr, write_ptr) < sizeof (T)) {
			break;
		}

		wp = &this->_buf[read_ptr];
		memcpy (&ev_time, wp, sizeof (T));
		read_ptr = (read_ptr + sizeof (T)) % this->_size;
		str << "time " << ev_time;

		write_ptr  = g_atomic_int_get (&this->_write_ptr);
		if (space (read_ptr, write_ptr) < sizeof (ev_type)) {
			break;
		}

		wp = &this->_buf[read_ptr];
		memcpy (&ev_type, wp, sizeof (ev_type));
		read_ptr = (read_ptr + sizeof (ev_type)) % this->_size;
		str << " type " << ev_type;

		write_ptr  = g_atomic_int_get (&this->_write_ptr);
		if (space (read_ptr, write_ptr) < sizeof (ev_size)) {
			str << "!OUT!\n";
			break;
		}

		wp = &this->_buf[read_ptr];
		memcpy (&ev_size, wp, sizeof (ev_size));
		read_ptr = (read_ptr + sizeof (ev_size)) % this->_size;
		str << " size " << ev_size;

		write_ptr  = g_atomic_int_get (&this->_write_ptr);
		if (space (read_ptr, write_ptr) < ev_size) {
			str << "!OUT!\n";
			break;
		}

		data = new uint8_t[ev_size];

		wp = &this->_buf[read_ptr];
		memcpy (data, wp, ev_size);
		read_ptr = (read_ptr + ev_size) % this->_size;

		for (uint32_t i = 0; i != ev_size; ++i) {
			str << ' ' << hex << (int) data[i] << dec;
		}

		str << endl;

		delete [] data;
	}
}


template class MidiRingBuffer<framepos_t>;

