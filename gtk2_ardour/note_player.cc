/*
    Copyright (C) 2011 Paul Davis

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

#include <sigc++/bind.h>
#include <glibmm/main.h>

#include "ardour/midi_track.h"
#include "ardour/session.h"

#include "note_player.h"

using namespace ARDOUR;
using namespace std;

NotePlayer::NotePlayer (boost::shared_ptr<MidiTrack> mt)
	: track (mt)
{
}

void
NotePlayer::add (boost::shared_ptr<NoteType> note)
{
	notes.push_back (note);
}

void
NotePlayer::play ()
{
	Evoral::MusicalTime longest_duration_beats = 0;

	/* note: if there is more than 1 note, we will silence them all at the same time
	 */

	for (Notes::iterator n = notes.begin(); n != notes.end(); ++n) {
		track->write_immediate_event ((*n)->on_event().size(), (*n)->on_event().buffer());
		if ((*n)->length() > longest_duration_beats) {
			longest_duration_beats = (*n)->length();
		}
	}

	uint32_t note_length_ms = 350;
	/* beats_to_frames (longest_duration_beats)
	 * (1000 / (double)track->session().nominal_frame_rate()); */

	Glib::signal_timeout().connect(sigc::bind (sigc::ptr_fun (&NotePlayer::_off), this),
	                               note_length_ms, G_PRIORITY_DEFAULT);
}

bool
NotePlayer::_off (NotePlayer* np)
{
	np->off ();
	delete np;
	return false;
}

void
NotePlayer::off ()
{
	for (Notes::iterator n = notes.begin(); n != notes.end(); ++n) {
		track->write_immediate_event((*n)->off_event().size(), (*n)->off_event().buffer());
	}
}
