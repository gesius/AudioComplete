/*
    Copyright (C) 2006-2009 Paul Davis 
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

#ifndef __ardour_midi_buffer_h__
#define __ardour_midi_buffer_h__

#include <evoral/midi_util.h>
#include <midi++/event.h>
#include <ardour/buffer.h>
#include <ardour/event_type_map.h>

namespace ARDOUR {


/** Buffer containing 8-bit unsigned char (MIDI) data. */
class MidiBuffer : public Buffer
{
public:
	typedef nframes_t TimeType;

	MidiBuffer(size_t capacity);
	~MidiBuffer();

	void silence(nframes_t dur, nframes_t offset=0);
	
	void read_from(const Buffer& src, nframes_t nframes, nframes_t offset);
	
	void copy(const MidiBuffer& copy);

	bool     push_back(const Evoral::MIDIEvent<TimeType>& event);
	bool     push_back(const jack_midi_event_t& event);
	uint8_t* reserve(TimeType time, size_t size);

	void resize(size_t);

	bool merge(const MidiBuffer& a, const MidiBuffer& b);
	bool merge_in_place(const MidiBuffer &other);
	
	template<typename BufferType, typename MIDIEventType>
	struct iterator_base {
		iterator_base<BufferType, MIDIEventType>(BufferType& b, size_t o) : buffer(b), offset(o) {}
		inline MIDIEventType operator*() const {
			uint8_t* ev_start = buffer._data + offset + sizeof(TimeType);
			int event_size = Evoral::midi_event_size(ev_start);
			assert(event_size >= 0);
			return MIDIEventType(EventTypeMap::instance().midi_event_type(*ev_start),
					*((TimeType*)(buffer._data + offset)),
					event_size, ev_start);
		}
		inline iterator_base<BufferType, MIDIEventType>& operator++() {
			uint8_t* ev_start = buffer._data + offset + sizeof(TimeType);
			int event_size = Evoral::midi_event_size(ev_start);
			assert(event_size >= 0);
			offset += sizeof(TimeType) + event_size;
			return *this;
		}
		inline bool operator!=(const iterator_base<BufferType, MIDIEventType>& other) const {
			return (&buffer != &other.buffer) || (offset != other.offset);
		}
		BufferType&     buffer;
		size_t          offset;
	};
	
	typedef iterator_base< MidiBuffer, Evoral::MIDIEvent<TimeType> >             iterator;
	typedef iterator_base< const MidiBuffer, const Evoral::MIDIEvent<TimeType> > const_iterator;

	iterator begin() { return iterator(*this, 0); }
	iterator end()   { return iterator(*this, _size); }

	const_iterator begin() const { return const_iterator(*this, 0); }
	const_iterator end()   const { return const_iterator(*this, _size); }

private:
	friend class iterator_base< MidiBuffer, Evoral::MIDIEvent<TimeType> >;
	friend class iterator_base< const MidiBuffer, const Evoral::MIDIEvent<TimeType> >;
	
	size_t   _size; ///< Size in bytes of used portion of _data
	uint8_t* _data; ///< timestamp, event, timestamp, event, ...
};


} // namespace ARDOUR

#endif // __ardour_midi_buffer_h__
