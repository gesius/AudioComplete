/*
    Copyright (C) 2002 Paul Davis

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

#include <algorithm>
#include <sigc++/bind.h>
#include "pbd/error.h"
#include "pbd/stacktrace.h"

#include "ardour/playlist.h"
#include "ardour/rc_configuration.h"

#include "gui_thread.h"
#include "midi_cut_buffer.h"
#include "region_view.h"
#include "selection.h"
#include "selection_templates.h"
#include "time_axis_view.h"
#include "automation_time_axis.h"
#include "public_editor.h"
#include "control_point.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

struct AudioRangeComparator {
    bool operator()(AudioRange a, AudioRange b) {
	    return a.start < b.start;
    }
};

Selection::Selection (const PublicEditor* e)
	: tracks (e)
	, editor (e)
	, next_time_id (0)
	, _no_tracks_changed (false)
{
	clear ();

	/* we have disambiguate which remove() for the compiler */

	void (Selection::*track_remove)(TimeAxisView*) = &Selection::remove;
	TimeAxisView::CatchDeletion.connect (*this, MISSING_INVALIDATOR, ui_bind (track_remove, this, _1), gui_context());

	void (Selection::*marker_remove)(Marker*) = &Selection::remove;
	Marker::CatchDeletion.connect (*this, MISSING_INVALIDATOR, ui_bind (marker_remove, this, _1), gui_context());
}

#if 0
Selection&
Selection::operator= (const Selection& other)
{
	if (&other != this) {
		regions = other.regions;
		tracks = other.tracks;
		time = other.time;
		lines = other.lines;
		midi_regions = other.midi_regions;
		midi_notes = other.midi_notes;
	}
	return *this;
}
#endif

bool
operator== (const Selection& a, const Selection& b)
{
	return a.regions == b.regions &&
		a.tracks == b.tracks &&
		a.time == b.time &&
		a.lines == b.lines &&
		a.playlists == b.playlists &&
		a.midi_notes == b.midi_notes &&
		a.midi_regions == b.midi_regions;
}

/** Clear everything from the Selection */
void
Selection::clear ()
{
	clear_tracks ();
	clear_regions ();
	clear_points ();
	clear_lines();
	clear_time ();
	clear_playlists ();
	clear_midi_notes ();
	clear_midi_regions ();
}

void
Selection::dump_region_layers()
{
	cerr << "region selection layer dump" << endl;
	for (RegionSelection::iterator i = regions.begin(); i != regions.end(); ++i) {
		cerr << "layer: " << (int)(*i)->region()->layer() << endl;
	}
}


void
Selection::clear_regions ()
{
	if (!regions.empty()) {
		regions.clear_all ();
		RegionsChanged();
		if (Config->get_link_region_and_track_selection()) {
			clear_tracks ();
		}
	}
}

void
Selection::clear_tracks ()
{
	if (!tracks.empty()) {
		tracks.clear ();
		if (!_no_tracks_changed) {
			TracksChanged();
		}
	}
}

void
Selection::clear_midi_notes ()
{
	if (!midi_notes.empty()) {
		for (MidiNoteSelection::iterator x = midi_notes.begin(); x != midi_notes.end(); ++x) {
			delete *x;
		}
		midi_notes.clear ();
		MidiNotesChanged ();
	}
}

void
Selection::clear_midi_regions ()
{
	if (!midi_regions.empty()) {
		midi_regions.clear ();
		MidiRegionsChanged ();
	}
}

void
Selection::clear_time ()
{
	time.clear();

	TimeChanged ();
}

void
Selection::clear_playlists ()
{
	/* Selections own their playlists */

	for (PlaylistSelection::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		/* selections own their own regions, which are copies of the "originals". make them go away */
		(*i)->drop_regions ();
		(*i)->release ();
	}

	if (!playlists.empty()) {
		playlists.clear ();
		PlaylistsChanged();
	}
}

void
Selection::clear_lines ()
{
	if (!lines.empty()) {
		lines.clear ();
		LinesChanged();
	}
}

void
Selection::clear_markers ()
{
	if (!markers.empty()) {
		markers.clear ();
		MarkersChanged();
	}
}

void
Selection::toggle (boost::shared_ptr<Playlist> pl)
{
	PlaylistSelection::iterator i;

	if ((i = find (playlists.begin(), playlists.end(), pl)) == playlists.end()) {
		pl->use ();
		playlists.push_back(pl);
	} else {
		playlists.erase (i);
	}

	PlaylistsChanged ();
}

void
Selection::toggle (const TrackViewList& track_list)
{
	for (TrackViewList::const_iterator i = track_list.begin(); i != track_list.end(); ++i) {
		toggle ((*i));
	}
}

void
Selection::toggle (TimeAxisView* track)
{
	TrackSelection::iterator i;

	if ((i = find (tracks.begin(), tracks.end(), track)) == tracks.end()) {
		tracks.push_back (track);
	} else {
		tracks.erase (i);
	}

	if (!_no_tracks_changed) {
		TracksChanged();
	}
}

void
Selection::toggle (const MidiNoteSelection& midi_note_list)
{
	for (MidiNoteSelection::const_iterator i = midi_note_list.begin(); i != midi_note_list.end(); ++i) {
		toggle ((*i));
	}
}

void
Selection::toggle (MidiCutBuffer* midi)
{
	MidiNoteSelection::iterator i;

	if ((i = find (midi_notes.begin(), midi_notes.end(), midi)) == midi_notes.end()) {
		midi_notes.push_back (midi);
	} else {
		/* remember that we own the MCB */
		delete *i;
		midi_notes.erase (i);
	}

	MidiNotesChanged();
}


void
Selection::toggle (RegionView* r)
{
	RegionSelection::iterator i;

	if ((i = find (regions.begin(), regions.end(), r)) == regions.end()) {
		add (r);
	} else {
		remove (*i);
	}

	RegionsChanged ();
}

void
Selection::toggle (MidiRegionView* mrv)
{
	MidiRegionSelection::iterator i;

	if ((i = find (midi_regions.begin(), midi_regions.end(), mrv)) == midi_regions.end()) {
		add (mrv);
	} else {
		midi_regions.erase (i);
	}

	MidiRegionsChanged ();
}

void
Selection::toggle (vector<RegionView*>& r)
{
	RegionSelection::iterator i;

	for (vector<RegionView*>::iterator x = r.begin(); x != r.end(); ++x) {
		if ((i = find (regions.begin(), regions.end(), (*x))) == regions.end()) {
			add ((*x));
		} else {
			remove (*x);
		}
	}

	RegionsChanged ();
}

long
Selection::toggle (framepos_t start, framepos_t end)
{
	AudioRangeComparator cmp;

	/* XXX this implementation is incorrect */

	time.push_back (AudioRange (start, end, next_time_id++));
	time.consolidate ();
	time.sort (cmp);

	TimeChanged ();

	return next_time_id - 1;
}

void
Selection::add (boost::shared_ptr<Playlist> pl)
{
	if (find (playlists.begin(), playlists.end(), pl) == playlists.end()) {
		pl->use ();
		playlists.push_back(pl);
		PlaylistsChanged ();
	}
}

void
Selection::add (const list<boost::shared_ptr<Playlist> >& pllist)
{
	bool changed = false;

	for (list<boost::shared_ptr<Playlist> >::const_iterator i = pllist.begin(); i != pllist.end(); ++i) {
		if (find (playlists.begin(), playlists.end(), (*i)) == playlists.end()) {
			(*i)->use ();
			playlists.push_back (*i);
			changed = true;
		}
	}

	if (changed) {
		PlaylistsChanged ();
	}
}

void
Selection::add (const TrackViewList& track_list)
{
	TrackViewList added = tracks.add (track_list);

	if (!added.empty()) {
		if (!_no_tracks_changed) {
			TracksChanged ();
		}
	}
}

void
Selection::add (TimeAxisView* track)
{
	TrackViewList tr;
	tr.push_back (track);
	add (tr);
}

void
Selection::add (const MidiNoteSelection& midi_list)
{
	const MidiNoteSelection::const_iterator b = midi_list.begin();
	const MidiNoteSelection::const_iterator e = midi_list.end();

	if (!midi_list.empty()) {
		midi_notes.insert (midi_notes.end(), b, e);
		MidiNotesChanged ();
	}
}

void
Selection::add (MidiCutBuffer* midi)
{
	/* we take ownership of the MCB */

	if (find (midi_notes.begin(), midi_notes.end(), midi) == midi_notes.end()) {
		midi_notes.push_back (midi);
		MidiNotesChanged ();
	}
}

void
Selection::add (vector<RegionView*>& v)
{
	/* XXX This method or the add (const RegionSelection&) needs to go
	 */

	bool changed = false;

	for (vector<RegionView*>::iterator i = v.begin(); i != v.end(); ++i) {
		if (find (regions.begin(), regions.end(), (*i)) == regions.end()) {
			changed = regions.add ((*i));
			if (Config->get_link_region_and_track_selection() && changed) {
				add (&(*i)->get_time_axis_view());
			}
		}
	}

	if (changed) {
		RegionsChanged ();
	}
}

void
Selection::add (const RegionSelection& rs)
{
	/* XXX This method or the add (const vector<RegionView*>&) needs to go
	 */

	bool changed = false;

	for (RegionSelection::const_iterator i = rs.begin(); i != rs.end(); ++i) {
		if (find (regions.begin(), regions.end(), (*i)) == regions.end()) {
			changed = regions.add ((*i));
			if (Config->get_link_region_and_track_selection() && changed) {
				add (&(*i)->get_time_axis_view());
			}
		}
	}

	if (changed) {
		RegionsChanged ();
	}
}

void
Selection::add (RegionView* r)
{
	if (find (regions.begin(), regions.end(), r) == regions.end()) {
		bool changed = regions.add (r);
                if (Config->get_link_region_and_track_selection() && changed) {
                        add (&r->get_time_axis_view());
                }
                if (changed) {
                        RegionsChanged ();
                }
	}
}

void
Selection::add (MidiRegionView* mrv)
{
	if (find (midi_regions.begin(), midi_regions.end(), mrv) == midi_regions.end()) {
		midi_regions.push_back (mrv);
		/* XXX should we do this? */
#if 0
		if (Config->get_link_region_and_track_selection()) {
			add (&mrv->get_time_axis_view());
		}
#endif
		MidiRegionsChanged ();
	}
}

long
Selection::add (framepos_t start, framepos_t end)
{
	AudioRangeComparator cmp;

	/* XXX this implementation is incorrect */

	time.push_back (AudioRange (start, end, next_time_id++));
	time.consolidate ();
	time.sort (cmp);

	TimeChanged ();

	return next_time_id - 1;
}

void
Selection::replace (uint32_t sid, framepos_t start, framepos_t end)
{
	for (list<AudioRange>::iterator i = time.begin(); i != time.end(); ++i) {
		if ((*i).id == sid) {
			time.erase (i);
			time.push_back (AudioRange(start,end, sid));

			/* don't consolidate here */


			AudioRangeComparator cmp;
			time.sort (cmp);

			TimeChanged ();
			break;
		}
	}
}

void
Selection::add (boost::shared_ptr<Evoral::ControlList> cl)
{
	boost::shared_ptr<ARDOUR::AutomationList> al
		= boost::dynamic_pointer_cast<ARDOUR::AutomationList>(cl);
	if (!al) {
		warning << "Programming error: Selected list is not an ARDOUR::AutomationList" << endmsg;
		return;
		return;
	}
	if (find (lines.begin(), lines.end(), al) == lines.end()) {
		lines.push_back (al);
		LinesChanged();
	}
}

void
Selection::remove (TimeAxisView* track)
{
	list<TimeAxisView*>::iterator i;
	if ((i = find (tracks.begin(), tracks.end(), track)) != tracks.end()) {
		tracks.erase (i);
		if (!_no_tracks_changed) {
			TracksChanged();
		}
	}
}

void
Selection::remove (const TrackViewList& track_list)
{
	bool changed = false;

	for (TrackViewList::const_iterator i = track_list.begin(); i != track_list.end(); ++i) {

		TrackViewList::iterator x = find (tracks.begin(), tracks.end(), *i);
		if (x != tracks.end()) {
			tracks.erase (x);
			changed = true;
		}
	}

	if (changed) {
		if (!_no_tracks_changed) {
			TracksChanged();
		}
	}
}

void
Selection::remove (const MidiNoteSelection& midi_list)
{
	bool changed = false;

	for (MidiNoteSelection::const_iterator i = midi_list.begin(); i != midi_list.end(); ++i) {

		MidiNoteSelection::iterator x;

		if ((x = find (midi_notes.begin(), midi_notes.end(), (*i))) != midi_notes.end()) {
			midi_notes.erase (x);
			changed = true;
		}
	}

	if (changed) {
		MidiNotesChanged();
	}
}

void
Selection::remove (MidiCutBuffer* midi)
{
	MidiNoteSelection::iterator x;

	if ((x = find (midi_notes.begin(), midi_notes.end(), midi)) != midi_notes.end()) {
		/* remember that we own the MCB */
		delete *x;
		midi_notes.erase (x);
		MidiNotesChanged ();
	}
}

void
Selection::remove (boost::shared_ptr<Playlist> track)
{
	list<boost::shared_ptr<Playlist> >::iterator i;
	if ((i = find (playlists.begin(), playlists.end(), track)) != playlists.end()) {
		playlists.erase (i);
		PlaylistsChanged();
	}
}

void
Selection::remove (const list<boost::shared_ptr<Playlist> >& pllist)
{
	bool changed = false;

	for (list<boost::shared_ptr<Playlist> >::const_iterator i = pllist.begin(); i != pllist.end(); ++i) {

		list<boost::shared_ptr<Playlist> >::iterator x;

		if ((x = find (playlists.begin(), playlists.end(), (*i))) != playlists.end()) {
			playlists.erase (x);
			changed = true;
		}
	}

	if (changed) {
		PlaylistsChanged();
	}
}

void
Selection::remove (RegionView* r)
{
	if (regions.remove (r)) {
		RegionsChanged ();
	}

	if (Config->get_link_region_and_track_selection() && !regions.involves (r->get_time_axis_view())) {
		remove (&r->get_time_axis_view());
	}
}

void
Selection::remove (MidiRegionView* mrv)
{
	MidiRegionSelection::iterator x;

	if ((x = find (midi_regions.begin(), midi_regions.end(), mrv)) != midi_regions.end()) {
		midi_regions.erase (x);
		MidiRegionsChanged ();
	}

#if 0
	/* XXX fix this up ? */
	if (Config->get_link_region_and_track_selection() && !regions.involves (r->get_time_axis_view())) {
		remove (&r->get_time_axis_view());
	}
#endif
}


void
Selection::remove (uint32_t selection_id)
{
	if (time.empty()) {
		return;
	}

	for (list<AudioRange>::iterator i = time.begin(); i != time.end(); ++i) {
		if ((*i).id == selection_id) {
			time.erase (i);

			TimeChanged ();
			break;
		}
	}
}

void
Selection::remove (framepos_t /*start*/, framepos_t /*end*/)
{
}

void
Selection::remove (boost::shared_ptr<ARDOUR::AutomationList> ac)
{
	AutomationSelection::iterator i;
	if ((i = find (lines.begin(), lines.end(), ac)) != lines.end()) {
		lines.erase (i);
		LinesChanged();
	}
}

void
Selection::set (TimeAxisView* track)
{
	clear_tracks ();
	add (track);
}

void
Selection::set (const TrackViewList& track_list)
{
	clear_tracks ();
	add (track_list);
}

void
Selection::set (const MidiNoteSelection& midi_list)
{
	clear_midi_notes ();
	add (midi_list);
}

void
Selection::set (boost::shared_ptr<Playlist> playlist)
{
	clear_playlists ();
	add (playlist);
}

void
Selection::set (const list<boost::shared_ptr<Playlist> >& pllist)
{
	clear_playlists ();
	add (pllist);
}

void
Selection::set (const RegionSelection& rs)
{
	clear_regions();
	regions = rs;
	RegionsChanged(); /* EMIT SIGNAL */
}

void
Selection::set (MidiRegionView* mrv)
{
	clear_midi_regions ();
	add (mrv);
}

void
Selection::set (RegionView* r, bool also_clear_tracks)
{
	clear_regions ();
	if (also_clear_tracks && !Config->get_link_region_and_track_selection()) {
		/* clear_regions() will have done this if the link preference
		 * is enabled
		 */
		clear_tracks ();
	}
	add (r);
}

void
Selection::set (vector<RegionView*>& v)
{
	clear_regions ();
	if (Config->get_link_region_and_track_selection()) {
		clear_tracks ();
		// make sure to deselect any automation selections
		clear_points();
	}
	add (v);
}

/** Set the start and end time of the time selection, without changing
 *  the list of tracks it applies to.
 */
long
Selection::set (framepos_t start, framepos_t end)
{
	if ((start == 0 && end == 0) || end < start) {
		return 0;
	}

	if (time.empty()) {
		time.push_back (AudioRange (start, end, next_time_id++));
	} else {
		/* reuse the first entry, and remove all the rest */

		while (time.size() > 1) {
			time.pop_front();
		}
		time.front().start = start;
		time.front().end = end;
	}

	time.consolidate ();

	TimeChanged ();

	return time.front().id;
}

/** Set the start and end of the range selection.  If more than one range
 *  is currently selected, the start of the earliest range and the end of the
 *  latest range are set.  If no range is currently selected, this method
 *  selects a single range from start to end.
 *
 *  @param start New start time.
 *  @param end New end time.
 */
void
Selection::set_preserving_all_ranges (framepos_t start, framepos_t end)
{
	if ((start == 0 && end == 0) || (end < start)) {
		return;
	}

	if (time.empty ()) {
		time.push_back (AudioRange (start, end, next_time_id++));
	} else {
		time.sort (AudioRangeComparator ());
		time.front().start = start;
		time.back().end = end;
	}

	time.consolidate ();

	TimeChanged ();
}

void
Selection::set (boost::shared_ptr<Evoral::ControlList> ac)
{
	lines.clear();
	add (ac);
}

bool
Selection::selected (Marker* m)
{
	return find (markers.begin(), markers.end(), m) != markers.end();
}

bool
Selection::selected (TimeAxisView* tv)
{
	return find (tracks.begin(), tracks.end(), tv) != tracks.end();
}

bool
Selection::selected (RegionView* rv)
{
	return find (regions.begin(), regions.end(), rv) != regions.end();
}

bool
Selection::empty (bool internal_selection)
{
	bool object_level_empty =  regions.empty () &&
		tracks.empty () &&
		points.empty () &&
		playlists.empty () &&
		lines.empty () &&
		time.empty () &&
		playlists.empty () &&
		markers.empty() &&
		midi_regions.empty()
		;

	if (!internal_selection) {
		return object_level_empty;
	}

	/* this is intended to really only apply when using a Selection
	   as a cut buffer.
	*/

	return object_level_empty && midi_notes.empty();
}

void
Selection::toggle (ControlPoint* cp)
{
	cp->set_selected (!cp->get_selected ());
	set_point_selection_from_line (cp->line ());
}

void
Selection::toggle (vector<ControlPoint*> const & cps)
{
	for (vector<ControlPoint*>::const_iterator i = cps.begin(); i != cps.end(); ++i) {
		(*i)->set_selected (!(*i)->get_selected ());
	}

	set_point_selection_from_line (cps.front()->line ());
}

void
Selection::toggle (list<Selectable*> const & selectables)
{
	RegionView* rv;
	ControlPoint* cp;
	vector<RegionView*> rvs;
	vector<ControlPoint*> cps;

	for (std::list<Selectable*>::const_iterator i = selectables.begin(); i != selectables.end(); ++i) {
		if ((rv = dynamic_cast<RegionView*> (*i)) != 0) {
			rvs.push_back (rv);
		} else if ((cp = dynamic_cast<ControlPoint*> (*i)) != 0) {
			cps.push_back (cp);
		} else {
			fatal << _("programming error: ")
			      << X_("unknown selectable type passed to Selection::toggle()")
			      << endmsg;
			/*NOTREACHED*/
		}
	}

	if (!rvs.empty()) {
		toggle (rvs);
	}

	if (!cps.empty()) {
		toggle (cps);
	}
}

void
Selection::set (list<Selectable*> const & selectables)
{
	clear_regions();
	clear_points ();

	if (Config->get_link_region_and_track_selection ()) {
		clear_tracks ();
	}

	add (selectables);
}


void
Selection::add (list<Selectable*> const & selectables)
{
	RegionView* rv;
	ControlPoint* cp;
	vector<RegionView*> rvs;
	vector<ControlPoint*> cps;

	for (std::list<Selectable*>::const_iterator i = selectables.begin(); i != selectables.end(); ++i) {
		if ((rv = dynamic_cast<RegionView*> (*i)) != 0) {
			rvs.push_back (rv);
		} else if ((cp = dynamic_cast<ControlPoint*> (*i)) != 0) {
			cps.push_back (cp);
		} else {
			fatal << _("programming error: ")
			      << X_("unknown selectable type passed to Selection::add()")
			      << endmsg;
			/*NOTREACHED*/
		}
	}

	if (!rvs.empty()) {
		add (rvs);
	}

	if (!cps.empty()) {
		add (cps);
	}
}

void
Selection::clear_points ()
{
	if (!points.empty()) {
		points.clear ();
		PointsChanged ();
	}
}

void
Selection::add (ControlPoint* cp)
{
	cp->set_selected (true);
	set_point_selection_from_line (cp->line ());
}

void
Selection::add (vector<ControlPoint*> const & cps)
{
	for (vector<ControlPoint*>::const_iterator i = cps.begin(); i != cps.end(); ++i) {
		(*i)->set_selected (true);
	}

	set_point_selection_from_line (cps.front()->line ());
}

void
Selection::set (ControlPoint* cp)
{
	if (cp->get_selected()) {
		return;
	}

	/* We're going to set up the PointSelection from the selected ControlPoints
	   on this point's line, so we need to deselect all ControlPoints before
	   we re-add this one.
	*/

	for (uint32_t i = 0; i < cp->line().npoints(); ++i) {
		cp->line().nth (i)->set_selected (false);
	}

	vector<ControlPoint*> cps;
	cps.push_back (cp);
	add (cps);
}

void
Selection::set (Marker* m)
{
	clear_markers ();
	add (m);
}

void
Selection::toggle (Marker* m)
{
	MarkerSelection::iterator i;

	if ((i = find (markers.begin(), markers.end(), m)) == markers.end()) {
		add (m);
	} else {
		remove (m);
	}
}

void
Selection::remove (Marker* m)
{
	MarkerSelection::iterator i;

	if ((i = find (markers.begin(), markers.end(), m)) != markers.end()) {
		markers.erase (i);
		MarkersChanged();
	}
}

void
Selection::add (Marker* m)
{
	if (find (markers.begin(), markers.end(), m) == markers.end()) {
		markers.push_back (m);
		MarkersChanged();
	}
}

void
Selection::add (const list<Marker*>& m)
{
	markers.insert (markers.end(), m.begin(), m.end());
	markers.sort ();
	markers.unique ();
	
	MarkersChanged ();
}

void
MarkerSelection::range (framepos_t& s, framepos_t& e)
{
	s = max_framepos;
	e = 0;

	for (MarkerSelection::iterator i = begin(); i != end(); ++i) {

		if ((*i)->position() < s) {
			s = (*i)->position();
		}

		if ((*i)->position() > e) {
			e = (*i)->position();
		}
	}

	s = std::min (s, e);
	e = std::max (s, e);
}

/** Automation control point selection is mostly manipulated using the selected state
 *  of the ControlPoints themselves.  For example, to add a point to a selection, its
 *  ControlPoint is marked as selected and then this method is called.  It sets up
 *  our PointSelection from the selected ControlPoints of a given AutomationLine.
 *
 *  We can't use ControlPoints directly in the selection, as we need to express a
 *  selection of not just a visible ControlPoint but also (possibly) some invisible
 *  points nearby.  Hence the selection stores AutomationRanges, and these are synced
 *  with ControlPoint selection state using AutomationLine::set_selected_points.
 */

void
Selection::set_point_selection_from_line (AutomationLine const & line)
{
	points.clear ();

	AutomationRange current (DBL_MAX, 0, 1, 0, &line.trackview);

	for (uint32_t i = 0; i < line.npoints(); ++i) {
		ControlPoint const * cp = line.nth (i);

		if (cp->get_selected()) {
			/* x and y position of this control point in coordinates suitable for
			   an AutomationRange (ie model time and fraction of track height)
			*/
			double const x = (*(cp->model()))->when;
			double const y = 1 - (cp->get_y() / line.trackview.current_height ());

			/* work out the position of a rectangle the size of a control point centred
			   on this point
			*/

			double const size = cp->size ();
			double const x_size = line.time_converter().from (line.trackview.editor().pixel_to_frame (size));
			double const y_size = size / line.trackview.current_height ();

			double const x1 = max (0.0, x - x_size / 2);
			double const x2 = x + x_size / 2;
			double const y1 = max (0.0, y - y_size / 2);
			double const y2 = y + y_size / 2;

			/* extend the current AutomationRange to put this point in */
			current.start = min (current.start, x1);
			current.end = max (current.end, x2);
			current.low_fract = min (current.low_fract, y1);
			current.high_fract = max (current.high_fract, y2);

		} else {
			/* this point isn't selected; if the current AutomationRange has some
			   stuff in it, push it onto the list and make a new one
			*/
			if (current.start < DBL_MAX) {
				points.push_back (current);
				current = AutomationRange (DBL_MAX, 0, 1, 0, &line.trackview);
			}
		}
	}

	/* Maybe push the current AutomationRange, as above */
	if (current.start < DBL_MAX) {
		points.push_back (current);
		current = AutomationRange (DBL_MAX, 0, 1, 0, &line.trackview);
	}

	PointsChanged (); /* EMIT SIGNAL */
}

XMLNode&
Selection::get_state () const
{
	/* XXX: not complete; just sufficient to get track selection state
	   so that re-opening plugin windows for editor mixer strips works
	*/

	XMLNode* node = new XMLNode (X_("Selection"));

	for (TrackSelection::const_iterator i = tracks.begin(); i != tracks.end(); ++i) {
		RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*> (*i);
		AutomationTimeAxisView* atv = dynamic_cast<AutomationTimeAxisView*> (*i);
		if (rtv) {
			XMLNode* t = node->add_child (X_("RouteView"));
			t->add_property (X_("id"), atoi (rtv->route()->id().to_s().c_str()));
		} else if (atv) {
			XMLNode* t = node->add_child (X_("AutomationView"));
			t->add_property (X_("id"), atoi (atv->parent_route()->id().to_s().c_str()));
			t->add_property (X_("parameter"), EventTypeMap::instance().to_symbol (atv->parameter ()));
		}
	}

	for (MarkerSelection::const_iterator i = markers.begin(); i != markers.end(); ++i) {
		XMLNode* t = node->add_child (X_("Marker"));

		bool is_start;
		Location* loc = editor->find_location_from_marker (*i, is_start);

		t->add_property (X_("id"), atoi (loc->id().to_s().c_str()));
		t->add_property (X_("start"), is_start ? X_("yes") : X_("no"));
	}

	return *node;
}

int
Selection::set_state (XMLNode const & node, int)
{
	if (node.name() != X_("Selection")) {
		return -1;
	}

	XMLNodeList children = node.children ();
	for (XMLNodeList::const_iterator i = children.begin(); i != children.end(); ++i) {
		if ((*i)->name() == X_("RouteView")) {

			XMLProperty* prop_id = (*i)->property (X_("id"));
			assert (prop_id);
			PBD::ID id (prop_id->value ());
			RouteTimeAxisView* rtv = editor->get_route_view_by_route_id (id);
			if (rtv) {
				add (rtv);
			}

		} else if ((*i)->name() == X_("AutomationView")) {

			XMLProperty* prop_id = (*i)->property (X_("id"));
			XMLProperty* prop_parameter = (*i)->property (X_("parameter"));

			assert (prop_id);
			assert (prop_parameter);

			PBD::ID id (prop_id->value ());
			RouteTimeAxisView* rtv = editor->get_route_view_by_route_id (id);

			if (rtv) {
				boost::shared_ptr<AutomationTimeAxisView> atv = rtv->automation_child (EventTypeMap::instance().new_parameter (prop_parameter->value ()));

				/* the automation could be for an entity that was never saved
				   in the session file. Don't freak out if we can't find
				   it.
				*/

				if (atv) {
					add (atv.get());
				}
			}

		} else if ((*i)->name() == X_("Marker")) {

			XMLProperty* prop_id = (*i)->property (X_("id"));
			XMLProperty* prop_start = (*i)->property (X_("start"));
			assert (prop_id);
			assert (prop_start);

			PBD::ID id (prop_id->value ());
			Marker* m = editor->find_marker_from_location_id (id, string_is_affirmative (prop_start->value ()));
			if (m) {
				add (m);
			}
			
		}
		
	}

	return 0;
}

void
Selection::remove_regions (TimeAxisView* t)
{
	RegionSelection::iterator i = regions.begin();
	while (i != regions.end ()) {
		RegionSelection::iterator tmp = i;
		++tmp;

		if (&(*i)->get_time_axis_view() == t) {
			remove (*i);
		}

		i = tmp;
	}
}

void
Selection::block_tracks_changed (bool yn)
{
	_no_tracks_changed = yn;
}
