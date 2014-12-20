/*
    Copyright (C) 2006 Paul Davis
    Author: David Robillard

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

#ifndef __ardour_midi_playlist_h__
#define __ardour_midi_playlist_h__

#include <vector>
#include <list>

#include "ardour/ardour.h"
#include "ardour/playlist.h"
#include "evoral/Parameter.hpp"

namespace Evoral {
template<typename Time> class EventSink;
}

namespace ARDOUR
{

class Session;
class MidiRegion;
class Source;
class MidiStateTracker;

template<typename T> class MidiRingBuffer;

class LIBARDOUR_API MidiPlaylist : public ARDOUR::Playlist
{
public:
	MidiPlaylist (Session&, const XMLNode&, bool hidden = false);
	MidiPlaylist (Session&, std::string name, bool hidden = false);
	MidiPlaylist (boost::shared_ptr<const MidiPlaylist> other, std::string name, bool hidden = false);
	MidiPlaylist (boost::shared_ptr<const MidiPlaylist> other, framepos_t start, framecnt_t cnt,
	              std::string name, bool hidden = false);

	~MidiPlaylist ();

	framecnt_t read (Evoral::EventSink<framepos_t>& buf,
			 framepos_t start, framecnt_t cnt, uint32_t chan_n = 0);

	int set_state (const XMLNode&, int version);

	bool destroy_region (boost::shared_ptr<Region>);

	void set_note_mode (NoteMode m) { _note_mode = m; }

	std::set<Evoral::Parameter> contained_automation();

	/** Clear all note trackers. */
	void reset_note_trackers ();

	/** Resolve all pending notes and clear all note trackers.
	 *
	 * @param dst Sink to write note offs to.
	 * @param time Time stamp of all written note offs.
	 */
	void resolve_note_trackers (Evoral::EventSink<framepos_t>& dst, framepos_t time);

protected:

	void remove_dependents (boost::shared_ptr<Region> region);

private:
	void dump () const;

	bool region_changed (const PBD::PropertyChange&, boost::shared_ptr<Region>);

	NoteMode _note_mode;

	typedef std::map<Region*,MidiStateTracker*> NoteTrackers;
	NoteTrackers _note_trackers;

};

} /* namespace ARDOUR */

#endif	/* __ardour_midi_playlist_h__ */


