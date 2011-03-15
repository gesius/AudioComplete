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
#include "pbd/compose.h"
#include "pbd/debug.h"

#include "ardour/debug.h"
#include "ardour/midi_buffer.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

// FIXME: mirroring for MIDI buffers?
MidiBuffer::MidiBuffer(size_t capacity)
	: Buffer(DataType::MIDI, capacity)
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
MidiBuffer::read_from (const Buffer& src, framecnt_t nframes, framecnt_t dst_offset, framecnt_t src_offset)
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
		} else {
			cerr << "MIDI event @ " <<  ev.time() << " skipped, not within range "
			     << src_offset << " .. " << (nframes + src_offset) << endl;
		}
	}

	_silent = src.silent();
}

void
MidiBuffer::merge_from (const Buffer& src, framecnt_t /*nframes*/, framecnt_t /*dst_offset*/, framecnt_t /*src_offset*/)
{
	const MidiBuffer* mbuf = dynamic_cast<const MidiBuffer*>(&src);
	assert (mbuf);
	assert (mbuf != this);

	/* XXX use nframes, and possible offsets */
	merge_in_place (*mbuf);
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

#ifndef NDEBUG
	if (DEBUG::MidiIO & PBD::debug_bits) {
		DEBUG_STR_DECL(a);
		DEBUG_STR_APPEND(a, string_compose ("midibuffer %1 push event @ %2 sz %3 ", this, time, size));
		for (size_t i=0; i < size; ++i) {
			DEBUG_STR_APPEND(a,hex);
			DEBUG_STR_APPEND(a,"0x");
			DEBUG_STR_APPEND(a,(int)data[i]);
			DEBUG_STR_APPEND(a,' ');
		}
		DEBUG_STR_APPEND(a,'\n');
		DEBUG_TRACE (DEBUG::MidiIO, DEBUG_STR(a).str());
	}
#endif

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
MidiBuffer::silence (framecnt_t /*nframes*/, framecnt_t /*offset*/)
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

#ifndef NDEBUG
	size_t   test_orig_us_size   = _size;
	size_t   test_orig_them_size = other._size;
	TimeType test_time           = 0;
	size_t   test_us_count       = 0;
	size_t   test_them_count     = 0;
	for (iterator i = begin(); i != end(); ++i) {
		assert(Evoral::midi_event_is_valid((*i).buffer(), (*i).size()));
		assert((*i).time() >= test_time);
		test_time = (*i).time();
		++test_us_count;
	}
	test_time = 0;
	for (const_iterator i = other.begin(); i != other.end(); ++i) {
		assert(Evoral::midi_event_is_valid((*i).buffer(), (*i).size()));
		assert((*i).time() >= test_time);
		test_time = (*i).time();
		++test_them_count;
	}
#endif

	const_iterator them = other.begin();
	iterator us = begin();

	while (them != other.end()) {

		size_t sz = 0;
		ssize_t src = -1;

		/* gather up total size of events that are earlier than
		   the event referenced by "us"
		*/

		while (them != other.end() && (*them).time() <= (*us).time()) {
			if (src == -1) {
				src = them.offset;
			}
			sz += sizeof (TimeType) + (*them).size();
			++them;
		}

#if 0
		if (us != end())
			cerr << "us @ " << (*us).time() << endl;
		if (them != other.end())
			cerr << "them @ " << (*them).time() << endl;
#endif

		if (sz) {
			assert(src >= 0);
			/* move existing */
			memmove (_data + us.offset + sz, _data + us.offset, _size - us.offset);
			/* increase _size */
			_size += sz;
			assert(_size <= _capacity);
			/* insert new stuff */
			memcpy  (_data + us.offset, other._data + src, sz);
			/* update iterator to our own events. this is a miserable hack */
			us.offset += sz;
		} else {

			/* advance past our own events to get to the correct insertion
			   point for the next event(s) from "other"
			*/

			while (us != end() && (*us).time() < (*them).time()) {
				++us;
			}
		}

		if (!(us != end())) {
			/* just append the rest of other */
			memcpy (_data + us.offset, other._data + them.offset, other._size - them.offset);
			_size += other._size - them.offset;
			break;
		}
	}

#ifndef NDEBUG
	assert(_size == test_orig_us_size + test_orig_them_size);
	size_t test_final_count = 0;
	test_time = 0;
	for (iterator i = begin(); i != end(); ++i) {
		// cerr << "CHECK " << test_final_count << " / " << test_us_count + test_them_count << endl;
		assert(Evoral::midi_event_is_valid((*i).buffer(), (*i).size()));
		assert((*i).time() >= test_time);
		test_time = (*i).time();
		++test_final_count;
	}
	assert(test_final_count = test_us_count + test_them_count);
#endif

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
	    return merge_in_place(b);
	} else if (this == &b) {
	    return merge_in_place(a);
	}

	const_iterator ai = a.begin();
	const_iterator bi = b.begin();

	resize(a.size() + b.size());
	while (ai != a.end() && bi != b.end()) {
		if ((*ai).time() < (*bi).time()) {
			memcpy(_data + _size, (*ai).buffer(), (*ai).size());
			_size += (*ai).size();
			++ai;
		} else {
			memcpy(_data + _size, (*bi).buffer(), (*bi).size());
			_size += (*bi).size();
			++bi;
		}
	}

	while (ai != a.end()) {
		memcpy(_data + _size, (*ai).buffer(), (*ai).size());
		_size += (*ai).size();
		++ai;
	}

	while (bi != b.end()) {
		memcpy(_data + _size, (*bi).buffer(), (*bi).size());
		_size += (*bi).size();
		++bi;
	}

	return true;
}

