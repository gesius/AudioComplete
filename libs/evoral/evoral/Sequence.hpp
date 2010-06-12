/* This file is part of Evoral.
 * Copyright (C) 2008 Dave Robillard <http://drobilla.net>
 * Copyright (C) 2000-2008 Paul Davis
 *
 * Evoral is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * Evoral is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef EVORAL_SEQUENCE_HPP
#define EVORAL_SEQUENCE_HPP

#include <vector>
#include <queue>
#include <set>
#include <list>
#include <utility>
#include <boost/shared_ptr.hpp>
#include <glibmm/thread.h>
#include "evoral/types.hpp"
#include "evoral/Note.hpp"
#include "evoral/Parameter.hpp"
#include "evoral/ControlSet.hpp"
#include "evoral/ControlList.hpp"

namespace Evoral {

class TypeMap;
template<typename Time> class EventSink;
template<typename Time> class Note;
template<typename Time> class Event;

/** An iterator over (the x axis of) a 2-d double coordinate space.
 */
class ControlIterator {
public:
	ControlIterator(boost::shared_ptr<const ControlList> al, double ax, double ay)
		: list(al)
		, x(ax)
		, y(ay)
	{}

	boost::shared_ptr<const ControlList> list;
	double x;
	double y;
};


/** This is a higher level view of events, with separate representations for
 * notes (instead of just unassociated note on/off events) and controller data.
 * Controller data is represented as a list of time-stamped float values. */
template<typename Time>
class Sequence : virtual public ControlSet {
public:
	Sequence(const TypeMap& type_map);
        Sequence(const Sequence<Time>& other);

protected:
	struct WriteLockImpl {
		WriteLockImpl(Glib::RWLock& s, Glib::Mutex& c)
			: sequence_lock(new Glib::RWLock::WriterLock(s))
			, control_lock(new Glib::Mutex::Lock(c))
		{ }
		~WriteLockImpl() {
			delete sequence_lock;
			delete control_lock;
		}
		Glib::RWLock::WriterLock* sequence_lock;
		Glib::Mutex::Lock*        control_lock;
	};

public:
        typedef typename boost::shared_ptr<Evoral::Note<Time> >  NotePtr;
        typedef typename boost::shared_ptr<const Evoral::Note<Time> >  constNotePtr;

	typedef boost::shared_ptr<Glib::RWLock::ReaderLock> ReadLock;
	typedef boost::shared_ptr<WriteLockImpl>            WriteLock;

	virtual ReadLock  read_lock() const { return ReadLock(new Glib::RWLock::ReaderLock(_lock)); }
	virtual WriteLock write_lock()      { return WriteLock(new WriteLockImpl(_lock, _control_lock)); }

	void clear();

	bool percussive() const     { return _percussive; }
	void set_percussive(bool p) { _percussive = p; }

	void start_write();
	bool writing() const { return _writing; }
	void end_write(bool delete_stuck=false);

	void append(const Event<Time>& ev);

	inline size_t n_notes() const { return _notes.size(); }
	inline bool   empty()   const { return _notes.size() == 0 && ControlSet::controls_empty(); }

	inline static bool note_time_comparator(const boost::shared_ptr< const Note<Time> >& a,
	                                        const boost::shared_ptr< const Note<Time> >& b) {
		return a->time() < b->time();
	}

	struct NoteNumberComparator {
		inline bool operator()(const boost::shared_ptr< const Note<Time> > a,
		                       const boost::shared_ptr< const Note<Time> > b) const {
			return a->note() < b->note();
		}
	};

	struct EarlierNoteComparator {
		inline bool operator()(const boost::shared_ptr< const Note<Time> > a,
		                       const boost::shared_ptr< const Note<Time> > b) const {
			return a->time() < b->time();
		}
	};

	struct LaterNoteComparator {
		typedef const Note<Time>* value_type;
		inline bool operator()(const boost::shared_ptr< const Note<Time> > a,
		                       const boost::shared_ptr< const Note<Time> > b) const {
			return a->time() > b->time();
		}
	};

	struct LaterNoteEndComparator {
		typedef const Note<Time>* value_type;
		inline bool operator()(const boost::shared_ptr< const Note<Time> > a,
		                       const boost::shared_ptr< const Note<Time> > b) const {
			return a->end_time() > b->end_time();
		}
	};

	typedef std::multiset<NotePtr, EarlierNoteComparator> Notes;
	inline       Notes& notes()       { return _notes; }
	inline const Notes& notes() const { return _notes; }

        enum NoteOperator { 
                PitchEqual,
                PitchLessThan,
                PitchLessThanOrEqual,
                PitchGreater,
                PitchGreaterThanOrEqual,
                VelocityEqual,
                VelocityLessThan,
                VelocityLessThanOrEqual,
                VelocityGreater,
                VelocityGreaterThanOrEqual,
        };

        void get_notes (Notes&, NoteOperator, uint8_t val, int chan_mask = 0) const;

        void remove_overlapping_notes ();
        void trim_overlapping_notes ();
        void remove_duplicate_notes ();

        enum OverlapPitchResolution { 
                LastOnFirstOff,
                FirstOnFirstOff
        };

        bool overlapping_pitches_accepted() const { return _overlapping_pitches_accepted; }
        void overlapping_pitches_accepted(bool yn)  { _overlapping_pitches_accepted = yn; }
        OverlapPitchResolution overlap_pitch_resolution() const { return _overlap_pitch_resolution; }
        void set_overlap_pitch_resolution(OverlapPitchResolution opr);

	void set_notes (const Sequence<Time>::Notes& n);

	typedef std::vector< boost::shared_ptr< Event<Time> > > SysExes;
	inline       SysExes& sysexes()       { return _sysexes; }
	inline const SysExes& sysexes() const { return _sysexes; }

private:
	typedef std::priority_queue<NotePtr, std::deque<NotePtr>, LaterNoteEndComparator> ActiveNotes;
public:

	/** Read iterator */
	class const_iterator {
	public:
		const_iterator();
		const_iterator(const Sequence<Time>& seq, Time t);
		~const_iterator();

		inline bool valid() const { return !_is_end && _event; }
		//inline bool locked() const { return _locked; }

		void invalidate();

		const Event<Time>& operator*()  const { return *_event;  }
		const boost::shared_ptr< Event<Time> > operator->() const  { return _event; }
		const boost::shared_ptr< Event<Time> > get_event_pointer() { return _event; }

		const const_iterator& operator++(); // prefix only

		bool operator==(const const_iterator& other) const;
		bool operator!=(const const_iterator& other) const { return ! operator==(other); }

		const_iterator& operator=(const const_iterator& other);

	private:
		friend class Sequence<Time>;

		typedef std::vector<ControlIterator> ControlIterators;
		enum MIDIMessageType { NIL, NOTE_ON, NOTE_OFF, CONTROL, SYSEX };

		const Sequence<Time>*            _seq;
		boost::shared_ptr< Event<Time> > _event;
		mutable ActiveNotes              _active_notes;
		MIDIMessageType                  _type;
		bool                             _is_end;
		typename Sequence::ReadLock      _lock;
		typename Notes::const_iterator   _note_iter;
		typename SysExes::const_iterator _sysex_iter;
		ControlIterators                 _control_iters;
		ControlIterators::iterator       _control_iter;
	};

	const_iterator        begin(Time t=0) const { return const_iterator(*this, t); }
	const const_iterator& end()           const { return _end_iter; }

	typename Notes::const_iterator note_lower_bound (Time t) const;

	bool control_to_midi_event(boost::shared_ptr< Event<Time> >& ev,
	                           const ControlIterator&            iter) const;

	bool edited() const      { return _edited; }
	void set_edited(bool yn) { _edited = yn; }

        bool overlaps (const NotePtr& ev, 
                       const NotePtr& ignore_this_note) const;
        bool contains (const NotePtr& ev) const;

        bool add_note_unlocked (const NotePtr note, std::set<NotePtr>* removed = 0);
	void remove_note_unlocked(const constNotePtr note);

	uint8_t lowest_note()  const { return _lowest_note; }
	uint8_t highest_note() const { return _highest_note; }


protected:
	bool                   _edited;
        bool                   _overlapping_pitches_accepted;
        OverlapPitchResolution _overlap_pitch_resolution;
	mutable Glib::RWLock   _lock;
	bool                   _writing;

        virtual int resolve_overlaps_unlocked (const NotePtr, std::set<NotePtr>* removed = 0) {
                return 0;
        }

	typedef std::multiset<NotePtr, NoteNumberComparator>  Pitches;
	inline       Pitches& pitches(uint8_t chan)       { return _pitches[chan&0xf]; }
        inline const Pitches& pitches(uint8_t chan) const { return _pitches[chan&0xf]; }

private:
	friend class const_iterator;

        bool overlaps_unlocked (const NotePtr& ev, const NotePtr& ignore_this_note) const;
        bool contains_unlocked (const NotePtr& ev) const;

        void append_note_on_unlocked (NotePtr);
        void append_note_off_unlocked(NotePtr);
	void append_control_unlocked(const Parameter& param, Time time, double value);
	void append_sysex_unlocked(const MIDIEvent<Time>& ev);

        void get_notes_by_pitch (Notes&, NoteOperator, uint8_t val, int chan_mask = 0) const;
        void get_notes_by_velocity (Notes&, NoteOperator, uint8_t val, int chan_mask = 0) const;

	const TypeMap& _type_map;

        Notes   _notes;       // notes indexed by time
        Pitches _pitches[16]; // notes indexed by channel+pitch
	SysExes _sysexes;

	typedef std::multiset<NotePtr, EarlierNoteComparator> WriteNotes;
	WriteNotes _write_notes[16];

	typedef std::vector< boost::shared_ptr<const ControlList> > ControlLists;
	ControlLists _dirty_controls;

	const   const_iterator _end_iter;
	bool                   _percussive;

	uint8_t _lowest_note;
	uint8_t _highest_note;
};


} // namespace Evoral

#endif // EVORAL_SEQUENCE_HPP

