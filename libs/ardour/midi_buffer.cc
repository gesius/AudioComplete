/*
    Copyright (C) 2006-2007 Paul Davis 
	Author: Dave Robillard
    
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
#include "pbd/malign.h"
#include "ardour/midi_buffer.h"

using namespace std;
using namespace ARDOUR;

// FIXME: mirroring for MIDI buffers?
MidiBuffer::MidiBuffer(size_t capacity)
	: Buffer(DataType::MIDI, capacity)
	, _size(0)
	, _data(0)
{
	if (capacity) {
		resize(_capacity);
		silence(_capacity);
	}
}
	
MidiBuffer::~MidiBuffer()
{
	free(_data);
}

void
MidiBuffer::resize(size_t size)
{
	assert(size > 0);

	if (size < _capacity) {
		return;
	}

	free(_data);

	_size = 0;
	_capacity = size;
	cache_aligned_malloc ((void**) &_data, _capacity);

	assert(_data);
}

void
MidiBuffer::copy(const MidiBuffer& copy)
{
	assert(_capacity >= copy._size);
	_size = copy._size;
	memcpy(_data, copy._data, copy._size);
}


/** Read events from @a src starting at time @a offset into the START of this buffer, for
 * time duration @a nframes.  Relative time, where 0 = start of buffer.
 *
 * Note that offset and nframes refer to sample time, NOT buffer offsets or event counts.
 */
void
MidiBuffer::read_from (const Buffer& src, nframes_t nframes, nframes_t dst_offset, nframes_t src_offset)
{
	assert (src.type() == DataType::MIDI);
	assert (&src != this);

	const MidiBuffer& msrc = (MidiBuffer&) src;
	
	assert (_capacity >= msrc.size());

	if (dst_offset == 0) {
		clear ();
		assert (_size == 0);
	}

	/* XXX use dst_offset somehow */
	
	for (MidiBuffer::const_iterator i = msrc.begin(); i != msrc.end(); ++i) {
		const Evoral::MIDIEvent<TimeType> ev(*i, false);
		if (ev.time() >= src_offset && ev.time() < (nframes+src_offset)) {
			push_back (ev);
		}
	}

	_silent = src.silent();
}

/** Push an event into the buffer.
 *
 * Note that the raw MIDI pointed to by ev will be COPIED and unmodified.
 * That is, the caller still owns it, if it needs freeing it's Not My Problem(TM).
 * Realtime safe.
 * @return false if operation failed (not enough room)
 */
bool
MidiBuffer::push_back(const Evoral::MIDIEvent<TimeType>& ev)
{
	const size_t stamp_size = sizeof(TimeType);
	/*cerr << "MidiBuffer: pushing event @ " << ev.time()
		<< " size = " << ev.size() << endl;*/
	
	if (_size + stamp_size + ev.size() >= _capacity) {
		cerr << "MidiBuffer::push_back failed (buffer is full)" << endl;
		return false;
	}

	if (!Evoral::midi_event_is_valid(ev.buffer(), ev.size())) {
		cerr << "WARNING: MidiBuffer ignoring illegal MIDI event" << endl;
		return false;
	}

	push_back(ev.time(), ev.size(), ev.buffer());
	
	return true;
}


/** Push an event into the buffer.
 * @return false if operation failed (not enough room)
 */
bool
MidiBuffer::push_back(TimeType time, size_t size, const uint8_t* data)
{
	const size_t stamp_size = sizeof(TimeType);
	/*cerr << "MidiBuffer: pushing event @ " << ev.time()
		<< " size = " << ev.size() << endl;*/
	
	if (_size + stamp_size + size >= _capacity) {
		cerr << "MidiBuffer::push_back failed (buffer is full)" << endl;
		return false;
	}

	if (!Evoral::midi_event_is_valid(data, size)) {
		cerr << "WARNING: MidiBuffer ignoring illegal MIDI event" << endl;
		return false;
	}

	uint8_t* const write_loc = _data + _size;
	*((TimeType*)write_loc) = time;
	memcpy(write_loc + stamp_size, data, size);

	_size += stamp_size + size;
	_silent = false;
	
	return true;
}


/** Push an event into the buffer.
 *
 * Note that the raw MIDI pointed to by ev will be COPIED and unmodified.
 * That is, the caller still owns it, if it needs freeing it's Not My Problem(TM).
 * Realtime safe.
 * @return false if operation failed (not enough room)
 */
bool
MidiBuffer::push_back(const jack_midi_event_t& ev)
{
	const size_t stamp_size = sizeof(TimeType);
	if (_size + stamp_size + ev.size >= _capacity) {
		cerr << "MidiBuffer::push_back failed (buffer is full)" << endl;
		return false;
	}
	
	if (!Evoral::midi_event_is_valid(ev.buffer, ev.size)) {
		cerr << "WARNING: MidiBuffer ignoring illegal MIDI event" << endl;
		return false;
	}

	uint8_t* const write_loc = _data + _size;
	*((TimeType*)write_loc) = ev.time;
	memcpy(write_loc + stamp_size, ev.buffer, ev.size);

	_size += stamp_size + ev.size;
	_silent = false;
	
	return true;
}


/** Reserve space for a new event in the buffer.
 *
 * This call is for copying MIDI directly into the buffer, the data location
 * (of sufficient size to write \a size bytes) is returned, or 0 on failure.
 * This call MUST be immediately followed by a write to the returned data
 * location, or the buffer will be corrupted and very nasty things will happen.
 */
uint8_t*
MidiBuffer::reserve(TimeType time, size_t size)
{
	const size_t stamp_size = sizeof(TimeType);
	if (_size + stamp_size + size >= _capacity) {
		return 0;
	}

	// write timestamp
	uint8_t* write_loc = _data + _size;
	*((TimeType*)write_loc) = time;
	
	// move write_loc to begin of MIDI buffer data to write to
	write_loc += stamp_size;

	_size += stamp_size + size;
	_silent = false;
	
	return write_loc;
}


void
MidiBuffer::silence (nframes_t nframes, nframes_t offset)
{
	/* XXX iterate over existing events, find all in range given by offset & nframes,
	   and delete them.
	*/

	_size = 0;
	_silent = true;
}

/** Merge \a other into this buffer.  Realtime safe. */
bool
MidiBuffer::merge_in_place(const MidiBuffer &other)
{
	if (other.size() == 0) {
		return true;
	}

	if (_size == 0) {
		copy(other);
		return true;
	}

	if (_size + other.size() > _capacity) {
		cerr << "MidiBuffer::merge failed (no space)" << endl;
		return false;
	}
	
	cerr << "FIXME: MIDI BUFFER IN-PLACE MERGE" << endl;
	return true;
}

/** Clear, and merge \a a and \a b into this buffer.
 *
 * \return true if complete merge was successful
 */
bool
MidiBuffer::merge(const MidiBuffer& a, const MidiBuffer& b)
{
	_size = 0;
	
	if (this == &a) {
	    merge_in_place(b);
	}

	if (this == &b) {
	    merge_in_place(a);
	}
	
	cerr << "FIXME: MIDI BUFFER MERGE" << endl;
	return true;
}

