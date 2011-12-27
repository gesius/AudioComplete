/*
    Copyright (C) 2000-2003 Paul Davis

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

#include <stdint.h>
#include <set>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <climits>

#include <boost/lexical_cast.hpp>

#include "pbd/convert.h"
#include "pbd/failed_constructor.h"
#include "pbd/stacktrace.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "ardour/debug.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/region_sorters.h"
#include "ardour/playlist_factory.h"
#include "ardour/playlist_source.h"
#include "ardour/transient_detector.h"
#include "ardour/session_playlists.h"
#include "ardour/source_factory.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

namespace ARDOUR {
namespace Properties {
PBD::PropertyDescriptor<bool> regions;
}
}

struct ShowMeTheList {
    ShowMeTheList (boost::shared_ptr<Playlist> pl, const string& n) : playlist (pl), name (n) {}
    ~ShowMeTheList () {
	    cerr << ">>>>" << name << endl; playlist->dump(); cerr << "<<<<" << name << endl << endl;
    };
    boost::shared_ptr<Playlist> playlist;
    string name;
};



void
Playlist::make_property_quarks ()
{
	Properties::regions.property_id = g_quark_from_static_string (X_("regions"));
	DEBUG_TRACE (DEBUG::Properties, string_compose ("quark for regions = %1\n",
	                                                Properties::regions.property_id));
}

RegionListProperty::RegionListProperty (Playlist& pl)
	: SequenceProperty<std::list<boost::shared_ptr<Region> > > (Properties::regions.property_id, boost::bind (&Playlist::update, &pl, _1))
	, _playlist (pl)
{

}

RegionListProperty::RegionListProperty (RegionListProperty const & p)
	: PBD::SequenceProperty<std::list<boost::shared_ptr<Region> > > (p)
	, _playlist (p._playlist)
{

}

RegionListProperty *
RegionListProperty::clone () const
{
	return new RegionListProperty (*this);
}

RegionListProperty *
RegionListProperty::create () const
{
	return new RegionListProperty (_playlist);
}

void
RegionListProperty::get_content_as_xml (boost::shared_ptr<Region> region, XMLNode & node) const
{
	/* All regions (even those which are deleted) have their state saved by other
	   code, so we can just store ID here.
	*/

	node.add_property ("id", region->id().to_s ());
}

boost::shared_ptr<Region>
RegionListProperty::get_content_from_xml (XMLNode const & node) const
{
	XMLProperty const * prop = node.property ("id");
	assert (prop);

	PBD::ID id (prop->value ());

	boost::shared_ptr<Region> ret = _playlist.region_by_id (id);

	if (!ret) {
		ret = RegionFactory::region_by_id (id);
	}

	return ret;
}

Playlist::Playlist (Session& sess, string nom, DataType type, bool hide)
	: SessionObject(sess, nom)
	, regions (*this)
	, _type(type)
{
	init (hide);
	first_set_state = false;
	_name = nom;
	_set_sort_id ();
}

Playlist::Playlist (Session& sess, const XMLNode& node, DataType type, bool hide)
	: SessionObject(sess, "unnamed playlist")
	, regions (*this)
	, _type(type)
{
#ifndef NDEBUG
	const XMLProperty* prop = node.property("type");
	assert(!prop || DataType(prop->value()) == _type);
#endif

	init (hide);
	_name = "unnamed"; /* reset by set_state */
	_set_sort_id ();

	/* set state called by derived class */
}

Playlist::Playlist (boost::shared_ptr<const Playlist> other, string namestr, bool hide)
	: SessionObject(other->_session, namestr)
	, regions (*this)
	, _type(other->_type)
	, _orig_track_id (other->_orig_track_id)
{
	init (hide);

	RegionList tmp;
	other->copy_regions (tmp);

	in_set_state++;

	for (list<boost::shared_ptr<Region> >::iterator x = tmp.begin(); x != tmp.end(); ++x) {
		add_region_internal( (*x), (*x)->position());
	}

	in_set_state--;

	_splicing  = other->_splicing;
	_nudging   = other->_nudging;
	_edit_mode = other->_edit_mode;

	in_set_state = 0;
	first_set_state = false;
	in_flush = false;
	in_partition = false;
	subcnt = 0;
	_frozen = other->_frozen;

	layer_op_counter = other->layer_op_counter;
	freeze_length = other->freeze_length;
}

Playlist::Playlist (boost::shared_ptr<const Playlist> other, framepos_t start, framecnt_t cnt, string str, bool hide)
	: SessionObject(other->_session, str)
	, regions (*this)
	, _type(other->_type)
	, _orig_track_id (other->_orig_track_id)
{
	RegionLock rlock2 (const_cast<Playlist*> (other.get()));

	framepos_t end = start + cnt - 1;

	init (hide);

	in_set_state++;

	for (RegionList::const_iterator i = other->regions.begin(); i != other->regions.end(); ++i) {

		boost::shared_ptr<Region> region;
		boost::shared_ptr<Region> new_region;
		frameoffset_t offset = 0;
		framepos_t position = 0;
		framecnt_t len = 0;
		string    new_name;
		OverlapType overlap;

		region = *i;

		overlap = region->coverage (start, end);

		switch (overlap) {
		case OverlapNone:
			continue;

		case OverlapInternal:
			offset = start - region->position();
			position = 0;
			len = cnt;
			break;

		case OverlapStart:
			offset = 0;
			position = region->position() - start;
			len = end - region->position();
			break;

		case OverlapEnd:
			offset = start - region->position();
			position = 0;
			len = region->length() - offset;
			break;

		case OverlapExternal:
			offset = 0;
			position = region->position() - start;
			len = region->length();
			break;
		}

		RegionFactory::region_name (new_name, region->name(), false);

		PropertyList plist;

		plist.add (Properties::start, region->start() + offset);
		plist.add (Properties::length, len);
		plist.add (Properties::name, new_name);
		plist.add (Properties::layer, region->layer());

		new_region = RegionFactory::RegionFactory::create (region, plist);

		add_region_internal (new_region, position);
	}

	in_set_state--;
	first_set_state = false;
}

void
Playlist::use ()
{
	++_refcnt;
	InUse (true); /* EMIT SIGNAL */
}

void
Playlist::release ()
{
	if (_refcnt > 0) {
		_refcnt--;
	}

	if (_refcnt == 0) {
		InUse (false); /* EMIT SIGNAL */
	}
}

void
Playlist::copy_regions (RegionList& newlist) const
{
	RegionLock rlock (const_cast<Playlist *> (this));

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		newlist.push_back (RegionFactory::RegionFactory::create (*i, true));
	}
}

void
Playlist::init (bool hide)
{
	add_property (regions);
	_xml_node_name = X_("Playlist");

	g_atomic_int_set (&block_notifications, 0);
	g_atomic_int_set (&ignore_state_changes, 0);
	pending_contents_change = false;
	pending_layering = false;
	first_set_state = true;
	_refcnt = 0;
	_hidden = hide;
	_splicing = false;
	_shuffling = false;
	_nudging = false;
	in_set_state = 0;
	in_update = false;
	_edit_mode = Config->get_edit_mode();
	in_flush = false;
	in_partition = false;
	subcnt = 0;
	_frozen = false;
	layer_op_counter = 0;
	freeze_length = 0;
	_combine_ops = 0;
	_relayer_suspended = false;

	_session.history().BeginUndoRedo.connect_same_thread (*this, boost::bind (&Playlist::begin_undo, this));
	_session.history().EndUndoRedo.connect_same_thread (*this, boost::bind (&Playlist::end_undo, this));

	ContentsChanged.connect_same_thread (*this, boost::bind (&Playlist::mark_session_dirty, this));
}

Playlist::~Playlist ()
{
	DEBUG_TRACE (DEBUG::Destruction, string_compose ("Playlist %1 destructor\n", _name));

	{
		RegionLock rl (this);

		for (set<boost::shared_ptr<Region> >::iterator i = all_regions.begin(); i != all_regions.end(); ++i) {
			(*i)->set_playlist (boost::shared_ptr<Playlist>());
		}
	}

	/* GoingAway must be emitted by derived classes */
}

void
Playlist::_set_sort_id ()
{
	/*
	  Playlists are given names like <track name>.<id>
	  or <track name>.<edit group name>.<id> where id
	  is an integer. We extract the id and sort by that.
	*/

	size_t dot_position = _name.val().find_last_of(".");

	if (dot_position == string::npos) {
		_sort_id = 0;
	} else {
		string t = _name.val().substr(dot_position + 1);

		try {
			_sort_id = boost::lexical_cast<int>(t);
		}

		catch (boost::bad_lexical_cast e) {
			_sort_id = 0;
		}
	}
}

bool
Playlist::set_name (const string& str)
{
	/* in a typical situation, a playlist is being used
	   by one diskstream and also is referenced by the
	   Session. if there are more references than that,
	   then don't change the name.
	*/

	if (_refcnt > 2) {
		return false;
	}

	bool ret =  SessionObject::set_name(str);
	if (ret) {
		_set_sort_id ();
	}
	return ret;
}

/***********************************************************************
 CHANGE NOTIFICATION HANDLING

 Notifications must be delayed till the region_lock is released. This
 is necessary because handlers for the signals may need to acquire
 the lock (e.g. to read from the playlist).
 ***********************************************************************/

void
Playlist::begin_undo ()
{
	in_update = true;
	freeze ();
}

void
Playlist::end_undo ()
{
	thaw (true);
	in_update = false;
}

void
Playlist::freeze ()
{
	delay_notifications ();
	g_atomic_int_inc (&ignore_state_changes);
}

/** @param from_undo true if this thaw is triggered by the end of an undo on this playlist */
void
Playlist::thaw (bool from_undo)
{
	g_atomic_int_dec_and_test (&ignore_state_changes);
	release_notifications (from_undo);
}


void
Playlist::delay_notifications ()
{
	g_atomic_int_inc (&block_notifications);
	freeze_length = _get_extent().second;
}

/** @param from_undo true if this release is triggered by the end of an undo on this playlist */
void
Playlist::release_notifications (bool from_undo)
{
	if (g_atomic_int_dec_and_test (&block_notifications)) {
		flush_notifications (from_undo);
	}
}

void
Playlist::notify_contents_changed ()
{
	if (holding_state ()) {
		pending_contents_change = true;
	} else {
		pending_contents_change = false;
		ContentsChanged(); /* EMIT SIGNAL */
	}
}

void
Playlist::notify_layering_changed ()
{
	if (holding_state ()) {
		pending_layering = true;
	} else {
		pending_layering = false;
		LayeringChanged(); /* EMIT SIGNAL */
	}
}

void
Playlist::notify_region_removed (boost::shared_ptr<Region> r)
{
	if (holding_state ()) {
		pending_removes.insert (r);
		pending_contents_change = true;
	} else {
		/* this might not be true, but we have to act
		   as though it could be.
		*/
		pending_contents_change = false;
		RegionRemoved (boost::weak_ptr<Region> (r)); /* EMIT SIGNAL */
		ContentsChanged (); /* EMIT SIGNAL */
	}
}

void
Playlist::notify_region_moved (boost::shared_ptr<Region> r)
{
	Evoral::RangeMove<framepos_t> const move (r->last_position (), r->length (), r->position ());

	/* We could timestamp the region's layer op here, but we're doing it in region_bounds_changed */

	if (holding_state ()) {

		pending_range_moves.push_back (move);

	} else {

		list< Evoral::RangeMove<framepos_t> > m;
		m.push_back (move);
		RangesMoved (m, false);
	}

}

void
Playlist::notify_region_start_trimmed (boost::shared_ptr<Region> r)
{
	timestamp_layer_op (LayerOpBoundsChange, r);

	if (r->position() >= r->last_position()) {
		/* trimmed shorter */
		return;
	}

	Evoral::Range<framepos_t> const extra (r->position(), r->last_position());

	if (holding_state ()) {

		pending_region_extensions.push_back (extra);

	} else {

		list<Evoral::Range<framepos_t> > r;
		r.push_back (extra);
		RegionsExtended (r);

	}
}

void
Playlist::notify_region_end_trimmed (boost::shared_ptr<Region> r)
{
	timestamp_layer_op (LayerOpBoundsChange, r);

	if (r->length() < r->last_length()) {
		/* trimmed shorter */
	}

	Evoral::Range<framepos_t> const extra (r->position() + r->last_length(), r->position() + r->length());

	if (holding_state ()) {

		pending_region_extensions.push_back (extra);

	} else {

		list<Evoral::Range<framepos_t> > r;
		r.push_back (extra);
		RegionsExtended (r);
	}
}


void
Playlist::notify_region_added (boost::shared_ptr<Region> r)
{
	/* the length change might not be true, but we have to act
	   as though it could be.
	*/

	timestamp_layer_op (LayerOpAdd, r);

	if (holding_state()) {
		pending_adds.insert (r);
		pending_contents_change = true;
	} else {
		r->clear_changes ();
		pending_contents_change = false;
		RegionAdded (boost::weak_ptr<Region> (r)); /* EMIT SIGNAL */
		ContentsChanged (); /* EMIT SIGNAL */
		relayer (r);
	}
}

/** @param from_undo true if this flush is triggered by the end of an undo on this playlist */
void
Playlist::flush_notifications (bool from_undo)
{
	set<boost::shared_ptr<Region> > dependent_checks_needed;
	set<boost::shared_ptr<Region> >::iterator s;
	uint32_t regions_changed = false;

	if (in_flush) {
		return;
	}

	in_flush = true;

	/* We have:

	   pending_bounds:  regions whose bounds position and/or length changes
	   pending_removes: regions which were removed
	   pending_adds:    regions which were added
	   pending_length:  true if the playlist length might have changed
	   pending_contents_change: true if there was almost any change in the playlist
	   pending_range_moves: details of periods of time that have been moved about (when regions have been moved)

	*/

	if (!pending_bounds.empty() || !pending_removes.empty() || !pending_adds.empty()) {
		regions_changed = true;
	}

	/* Make a list of regions that need relayering */
	RegionList regions_to_relayer;

	for (RegionList::iterator r = pending_bounds.begin(); r != pending_bounds.end(); ++r) {
		regions_to_relayer.push_back (*r);
		dependent_checks_needed.insert (*r);
	}

	for (s = pending_removes.begin(); s != pending_removes.end(); ++s) {
		remove_dependents (*s);
		RegionRemoved (boost::weak_ptr<Region> (*s)); /* EMIT SIGNAL */
	}

	for (s = pending_adds.begin(); s != pending_adds.end(); ++s) {
		 /* don't emit RegionAdded signal until relayering is done,
		    so that the region is fully setup by the time
		    anyone hear's that its been added
		 */
		 dependent_checks_needed.insert (*s);
	 }

	 if (regions_changed || pending_contents_change) {
		 pending_contents_change = false;
		 ContentsChanged (); /* EMIT SIGNAL */
	 }

	 for (s = pending_adds.begin(); s != pending_adds.end(); ++s) {
		 (*s)->clear_changes ();
		 RegionAdded (boost::weak_ptr<Region> (*s)); /* EMIT SIGNAL */
		 regions_to_relayer.push_back (*s);
	 }

	 for (s = dependent_checks_needed.begin(); s != dependent_checks_needed.end(); ++s) {
		 check_dependents (*s, false);
	 }

	 if (!pending_range_moves.empty ()) {
		 RangesMoved (pending_range_moves, from_undo);
	 }

	 if (!pending_region_extensions.empty ()) {
		 RegionsExtended (pending_region_extensions);
	 }

	 if (!regions_to_relayer.empty ()) {
		 relayer (regions_to_relayer);
	 }

	 clear_pending ();

	 in_flush = false;
 }

 void
 Playlist::clear_pending ()
 {
	 pending_adds.clear ();
	 pending_removes.clear ();
	 pending_bounds.clear ();
	 pending_range_moves.clear ();
	 pending_region_extensions.clear ();
	 pending_contents_change = false;
 }

 /*************************************************************
   PLAYLIST OPERATIONS
  *************************************************************/

 void
 Playlist::add_region (boost::shared_ptr<Region> region, framepos_t position, float times, bool auto_partition)
 {
	 RegionLock rlock (this);
	 times = fabs (times);

	 int itimes = (int) floor (times);

	 framepos_t pos = position;

	 if (times == 1 && auto_partition){
		 partition(pos - 1, (pos + region->length()), true);
	 }

	 if (itimes >= 1) {
		 add_region_internal (region, pos);
		 pos += region->length();
		 --itimes;
	 }


	 /* note that itimes can be zero if we being asked to just
	    insert a single fraction of the region.
	 */

	 for (int i = 0; i < itimes; ++i) {
		 boost::shared_ptr<Region> copy = RegionFactory::create (region, true);
		 add_region_internal (copy, pos);
		 pos += region->length();
	 }

	 framecnt_t length = 0;

	 if (floor (times) != times) {
		 length = (framecnt_t) floor (region->length() * (times - floor (times)));
		 string name;
		 RegionFactory::region_name (name, region->name(), false);

		 {
			 PropertyList plist;

			 plist.add (Properties::start, region->start());
			 plist.add (Properties::length, length);
			 plist.add (Properties::name, name);
			 plist.add (Properties::layer, region->layer());

			 boost::shared_ptr<Region> sub = RegionFactory::create (region, plist);
			 add_region_internal (sub, pos);
		 }
	 }

	 possibly_splice_unlocked (position, (pos + length) - position, boost::shared_ptr<Region>());
 }

 void
 Playlist::set_region_ownership ()
 {
	 RegionLock rl (this);
	 RegionList::iterator i;
	 boost::weak_ptr<Playlist> pl (shared_from_this());

	 for (i = regions.begin(); i != regions.end(); ++i) {
		 (*i)->set_playlist (pl);
	 }
 }

 bool
 Playlist::add_region_internal (boost::shared_ptr<Region> region, framepos_t position)
 {
	 if (region->data_type() != _type) {
		 return false;
	 }

	 RegionSortByPosition cmp;

	 if (!first_set_state) {
		 boost::shared_ptr<Playlist> foo (shared_from_this());
		 region->set_playlist (boost::weak_ptr<Playlist>(foo));
	 }

	 region->set_position (position);

	 regions.insert (upper_bound (regions.begin(), regions.end(), region, cmp), region);
	 all_regions.insert (region);

	 possibly_splice_unlocked (position, region->length(), region);

	 /* we need to notify the existence of new region before checking dependents. Ick. */

	 notify_region_added (region);

	 if (!holding_state ()) {
		 check_dependents (region, false);
	 }

	 region->PropertyChanged.connect_same_thread (region_state_changed_connections, boost::bind (&Playlist::region_changed_proxy, this, _1, boost::weak_ptr<Region> (region)));

	 return true;
 }

 void
 Playlist::replace_region (boost::shared_ptr<Region> old, boost::shared_ptr<Region> newr, framepos_t pos)
 {
	 RegionLock rlock (this);

	 bool old_sp = _splicing;
	 _splicing = true;

	 remove_region_internal (old);
	 add_region_internal (newr, pos);

	 _splicing = old_sp;

	 possibly_splice_unlocked (pos, old->length() - newr->length());
 }

 void
 Playlist::remove_region (boost::shared_ptr<Region> region)
 {
	 RegionLock rlock (this);
	 remove_region_internal (region);
 }

 int
 Playlist::remove_region_internal (boost::shared_ptr<Region> region)
 {
	 RegionList::iterator i;

	 if (!in_set_state) {
		 /* unset playlist */
		 region->set_playlist (boost::weak_ptr<Playlist>());
	 }

	 /* XXX should probably freeze here .... */

	 for (i = regions.begin(); i != regions.end(); ++i) {
		 if (*i == region) {

			 framepos_t pos = (*i)->position();
			 framecnt_t distance = (*i)->length();

			 regions.erase (i);

			 possibly_splice_unlocked (pos, -distance);

			 if (!holding_state ()) {
				 remove_dependents (region);
			 }

			 notify_region_removed (region);
			 break;
		 }
	 }

	 return -1;
 }

 void
 Playlist::get_equivalent_regions (boost::shared_ptr<Region> other, vector<boost::shared_ptr<Region> >& results)
 {
	 if (Config->get_use_overlap_equivalency()) {
		 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
			 if ((*i)->overlap_equivalent (other)) {
				 results.push_back ((*i));
			 }
		 }
	 } else {
		 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
			 if ((*i)->equivalent (other)) {
				 results.push_back ((*i));
			 }
		 }
	 }
 }

 void
 Playlist::get_region_list_equivalent_regions (boost::shared_ptr<Region> other, vector<boost::shared_ptr<Region> >& results)
 {
	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {

		 if ((*i) && (*i)->region_list_equivalent (other)) {
			 results.push_back (*i);
		 }
	 }
 }

 void
 Playlist::partition (framepos_t start, framepos_t end, bool cut)
 {
	 RegionList thawlist;

	 partition_internal (start, end, cut, thawlist);

	 for (RegionList::iterator i = thawlist.begin(); i != thawlist.end(); ++i) {
		 (*i)->resume_property_changes ();
	 }
 }

/** Go through each region on the playlist and cut them at start and end, removing the section between
 *  start and end if cutting == true.  Regions that lie entirely within start and end are always
 *  removed.
 */
 void
 Playlist::partition_internal (framepos_t start, framepos_t end, bool cutting, RegionList& thawlist)
 {
	 RegionList new_regions;

	 /* Don't relayer regions that are created during this operation; leave them
	    on the same region as the original.
	 */
	 suspend_relayer ();

	 {
		 RegionLock rlock (this);

		 boost::shared_ptr<Region> region;
		 boost::shared_ptr<Region> current;
		 string new_name;
		 RegionList::iterator tmp;
		 OverlapType overlap;
		 framepos_t pos1, pos2, pos3, pos4;

		 in_partition = true;

		 /* need to work from a copy, because otherwise the regions we add during the process
		    get operated on as well.
		 */

		 RegionList copy = regions.rlist();

		 for (RegionList::iterator i = copy.begin(); i != copy.end(); i = tmp) {

			 tmp = i;
			 ++tmp;

			 current = *i;

			 if (current->first_frame() >= start && current->last_frame() < end) {

				 if (cutting) {
					 remove_region_internal (current);
				 }

				 continue;
			 }

			 /* coverage will return OverlapStart if the start coincides
			    with the end point. we do not partition such a region,
			    so catch this special case.
			 */

			 if (current->first_frame() >= end) {
				 continue;
			 }

			 if ((overlap = current->coverage (start, end)) == OverlapNone) {
				 continue;
			 }

			 pos1 = current->position();
			 pos2 = start;
			 pos3 = end;
			 pos4 = current->last_frame();

			 if (overlap == OverlapInternal) {
				 /* split: we need 3 new regions, the front, middle and end.
				    cut:   we need 2 regions, the front and end.
				 */

				 /*
					  start                 end
			   ---------------*************************------------
					  P1  P2              P3  P4
			   SPLIT:
			   ---------------*****++++++++++++++++====------------
			   CUT
			   ---------------*****----------------====------------

				 */

				 if (!cutting) {
					 /* "middle" ++++++ */

					 RegionFactory::region_name (new_name, current->name(), false);

					 PropertyList plist;

					 plist.add (Properties::start, current->start() + (pos2 - pos1));
					 plist.add (Properties::length, pos3 - pos2);
					 plist.add (Properties::name, new_name);
					 plist.add (Properties::layer, current->layer());
					 plist.add (Properties::automatic, true);
					 plist.add (Properties::left_of_split, true);
					 plist.add (Properties::right_of_split, true);

					 region = RegionFactory::create (current, plist);
					 add_region_internal (region, start);
					 new_regions.push_back (region);
				 }

				 /* "end" ====== */

				 RegionFactory::region_name (new_name, current->name(), false);

				 PropertyList plist;

				 plist.add (Properties::start, current->start() + (pos3 - pos1));
				 plist.add (Properties::length, pos4 - pos3);
				 plist.add (Properties::name, new_name);
				 plist.add (Properties::layer, current->layer());
				 plist.add (Properties::automatic, true);
				 plist.add (Properties::right_of_split, true);

				 region = RegionFactory::create (current, plist);

				 add_region_internal (region, end);
				 new_regions.push_back (region);

				 /* "front" ***** */

				 current->suspend_property_changes ();
				 thawlist.push_back (current);
				 current->cut_end (pos2 - 1);

			 } else if (overlap == OverlapEnd) {

				 /*
							       start           end
				     ---------------*************************------------
						    P1           P2         P4   P3
				     SPLIT:
				     ---------------**************+++++++++++------------
				     CUT:
				     ---------------**************-----------------------
				 */

				 if (!cutting) {

					 /* end +++++ */

					 RegionFactory::region_name (new_name, current->name(), false);

					 PropertyList plist;

					 plist.add (Properties::start, current->start() + (pos2 - pos1));
					 plist.add (Properties::length, pos4 - pos2);
					 plist.add (Properties::name, new_name);
					 plist.add (Properties::layer, current->layer());
					 plist.add (Properties::automatic, true);
					 plist.add (Properties::left_of_split, true);

					 region = RegionFactory::create (current, plist);

					 add_region_internal (region, start);
					 new_regions.push_back (region);
				 }

				 /* front ****** */

				 current->suspend_property_changes ();
				 thawlist.push_back (current);
				 current->cut_end (pos2 - 1);

			 } else if (overlap == OverlapStart) {

				 /* split: we need 2 regions: the front and the end.
				    cut: just trim current to skip the cut area
				 */

				 /*
							 start           end
				     ---------------*************************------------
					P2          P1 P3                   P4

				     SPLIT:
				     ---------------****+++++++++++++++++++++------------
				     CUT:
				     -------------------*********************------------

				 */

				 if (!cutting) {
					 /* front **** */
					 RegionFactory::region_name (new_name, current->name(), false);

					 PropertyList plist;

					 plist.add (Properties::start, current->start());
					 plist.add (Properties::length, pos3 - pos1);
					 plist.add (Properties::name, new_name);
					 plist.add (Properties::layer, current->layer());
					 plist.add (Properties::automatic, true);
					 plist.add (Properties::right_of_split, true);

					 region = RegionFactory::create (current, plist);

					 add_region_internal (region, pos1);
					 new_regions.push_back (region);
				 }

				 /* end */

				 current->suspend_property_changes ();
				 thawlist.push_back (current);
				 current->trim_front (pos3);
			 } else if (overlap == OverlapExternal) {

				 /* split: no split required.
				    cut: remove the region.
				 */

				 /*
					start                                      end
				     ---------------*************************------------
					P2          P1 P3                   P4

				     SPLIT:
				     ---------------*************************------------
				     CUT:
				     ----------------------------------------------------

				 */

				 if (cutting) {
					 remove_region_internal (current);
				 }

				 new_regions.push_back (current);
			 }
		 }

		 in_partition = false;
	 }

	 for (RegionList::iterator i = new_regions.begin(); i != new_regions.end(); ++i) {
		 check_dependents (*i, false);
	 }

	 resume_relayer ();
 }

 boost::shared_ptr<Playlist>
 Playlist::cut_copy (boost::shared_ptr<Playlist> (Playlist::*pmf)(framepos_t, framecnt_t,bool), list<AudioRange>& ranges, bool result_is_hidden)
 {
	 boost::shared_ptr<Playlist> ret;
	 boost::shared_ptr<Playlist> pl;
	 framepos_t start;

	 if (ranges.empty()) {
		 return boost::shared_ptr<Playlist>();
	 }

	 start = ranges.front().start;

	 for (list<AudioRange>::iterator i = ranges.begin(); i != ranges.end(); ++i) {

		 pl = (this->*pmf)((*i).start, (*i).length(), result_is_hidden);

		 if (i == ranges.begin()) {
			 ret = pl;
		 } else {

			 /* paste the next section into the nascent playlist,
			    offset to reflect the start of the first range we
			    chopped.
			 */

			 ret->paste (pl, (*i).start - start, 1.0f);
		 }
	 }

	 return ret;
 }

 boost::shared_ptr<Playlist>
 Playlist::cut (list<AudioRange>& ranges, bool result_is_hidden)
 {
	 boost::shared_ptr<Playlist> (Playlist::*pmf)(framepos_t,framecnt_t,bool) = &Playlist::cut;
	 return cut_copy (pmf, ranges, result_is_hidden);
 }

 boost::shared_ptr<Playlist>
 Playlist::copy (list<AudioRange>& ranges, bool result_is_hidden)
 {
	 boost::shared_ptr<Playlist> (Playlist::*pmf)(framepos_t,framecnt_t,bool) = &Playlist::copy;
	 return cut_copy (pmf, ranges, result_is_hidden);
 }

 boost::shared_ptr<Playlist>
 Playlist::cut (framepos_t start, framecnt_t cnt, bool result_is_hidden)
 {
	 boost::shared_ptr<Playlist> the_copy;
	 RegionList thawlist;
	 char buf[32];

	 snprintf (buf, sizeof (buf), "%" PRIu32, ++subcnt);
	 string new_name = _name;
	 new_name += '.';
	 new_name += buf;

	 if ((the_copy = PlaylistFactory::create (shared_from_this(), start, cnt, new_name, result_is_hidden)) == 0) {
		 return boost::shared_ptr<Playlist>();
	 }

	 partition_internal (start, start+cnt-1, true, thawlist);

	 for (RegionList::iterator i = thawlist.begin(); i != thawlist.end(); ++i) {
		 (*i)->resume_property_changes();
	 }

	 return the_copy;
 }

 boost::shared_ptr<Playlist>
 Playlist::copy (framepos_t start, framecnt_t cnt, bool result_is_hidden)
 {
	 char buf[32];

	 snprintf (buf, sizeof (buf), "%" PRIu32, ++subcnt);
	 string new_name = _name;
	 new_name += '.';
	 new_name += buf;

	 cnt = min (_get_extent().second - start, cnt);
	 return PlaylistFactory::create (shared_from_this(), start, cnt, new_name, result_is_hidden);
 }

 int
 Playlist::paste (boost::shared_ptr<Playlist> other, framepos_t position, float times)
 {
	 times = fabs (times);

	 {
		 RegionLock rl1 (this);
		 RegionLock rl2 (other.get());

		 int itimes = (int) floor (times);
		 framepos_t pos = position;
		 framecnt_t const shift = other->_get_extent().second;
		 layer_t top_layer = regions.size();

		 while (itimes--) {
			 for (RegionList::iterator i = other->regions.begin(); i != other->regions.end(); ++i) {
				 boost::shared_ptr<Region> copy_of_region = RegionFactory::create (*i, true);

				 /* put these new regions on top of all existing ones, but preserve
				    the ordering they had in the original playlist.
				 */

				 copy_of_region->set_layer (copy_of_region->layer() + top_layer);
				 add_region_internal (copy_of_region, (*i)->position() + pos);
			 }
			 pos += shift;
		 }
	 }

	 return 0;
 }


 void
 Playlist::duplicate (boost::shared_ptr<Region> region, framepos_t position, float times)
 {
	 times = fabs (times);

	 RegionLock rl (this);
	 int itimes = (int) floor (times);
	 framepos_t pos = position + 1;

	 while (itimes--) {
		 boost::shared_ptr<Region> copy = RegionFactory::create (region, true);
		 add_region_internal (copy, pos);
		 pos += region->length();
	 }

	 if (floor (times) != times) {
		 framecnt_t length = (framecnt_t) floor (region->length() * (times - floor (times)));
		 string name;
		 RegionFactory::region_name (name, region->name(), false);

		 {
			 PropertyList plist;

			 plist.add (Properties::start, region->start());
			 plist.add (Properties::length, length);
			 plist.add (Properties::name, name);

			 boost::shared_ptr<Region> sub = RegionFactory::create (region, plist);
			 add_region_internal (sub, pos);
		 }
	 }
 }

 void
 Playlist::shift (framepos_t at, frameoffset_t distance, bool move_intersected, bool ignore_music_glue)
 {
	 RegionLock rlock (this);
	 RegionList copy (regions.rlist());
	 RegionList fixup;

	 for (RegionList::iterator r = copy.begin(); r != copy.end(); ++r) {

		 if ((*r)->last_frame() < at) {
			 /* too early */
			 continue;
		 }

		 if (at > (*r)->first_frame() && at < (*r)->last_frame()) {
			 /* intersected region */
			 if (!move_intersected) {
				 continue;
			 }
		 }

		 /* do not move regions glued to music time - that
		    has to be done separately.
		 */

		 if (!ignore_music_glue && (*r)->position_lock_style() != AudioTime) {
			 fixup.push_back (*r);
			 continue;
		 }

		 (*r)->set_position ((*r)->position() + distance);
	 }

	 /* XXX: may not be necessary; Region::post_set should do this, I think */
	 for (RegionList::iterator r = fixup.begin(); r != fixup.end(); ++r) {
		 (*r)->recompute_position_from_lock_style ();
	 }
 }

 void
 Playlist::split (framepos_t at)
 {
	 RegionLock rlock (this);
	 RegionList copy (regions.rlist());

	 /* use a copy since this operation can modify the region list
	  */

	 for (RegionList::iterator r = copy.begin(); r != copy.end(); ++r) {
		 _split_region (*r, at);
	 }
 }

 void
 Playlist::split_region (boost::shared_ptr<Region> region, framepos_t playlist_position)
 {
	 RegionLock rl (this);
	 _split_region (region, playlist_position);
 }

 void
 Playlist::_split_region (boost::shared_ptr<Region> region, framepos_t playlist_position)
 {
	 if (!region->covers (playlist_position)) {
		 return;
	 }

	 if (region->position() == playlist_position ||
	     region->last_frame() == playlist_position) {
		 return;
	 }

	 boost::shared_ptr<Region> left;
	 boost::shared_ptr<Region> right;
	 frameoffset_t before;
	 frameoffset_t after;
	 string before_name;
	 string after_name;

	 /* split doesn't change anything about length, so don't try to splice */

	 bool old_sp = _splicing;
	 _splicing = true;

	 before = playlist_position - region->position();
	 after = region->length() - before;

	 RegionFactory::region_name (before_name, region->name(), false);

	 {
		 PropertyList plist;

		 plist.add (Properties::position, region->position ());
		 plist.add (Properties::length, before);
		 plist.add (Properties::name, before_name);
		 plist.add (Properties::left_of_split, true);

		 /* note: we must use the version of ::create with an offset here,
		    since it supplies that offset to the Region constructor, which
		    is necessary to get audio region gain envelopes right.
		 */
		 left = RegionFactory::create (region, 0, plist);
	 }

	 RegionFactory::region_name (after_name, region->name(), false);

	 {
		 PropertyList plist;

		 plist.add (Properties::position, region->position() + before);
		 plist.add (Properties::length, after);
		 plist.add (Properties::name, after_name);
		 plist.add (Properties::right_of_split, true);

		 /* same note as above */
		 right = RegionFactory::create (region, before, plist);
	 }

	 add_region_internal (left, region->position());
	 add_region_internal (right, region->position() + before);

	 finalize_split_region (region, left, right);

	 remove_region_internal (region);

	 _splicing = old_sp;
 }

 void
 Playlist::possibly_splice (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude)
 {
	 if (_splicing || in_set_state) {
		 /* don't respond to splicing moves or state setting */
		 return;
	 }

	 if (_edit_mode == Splice) {
		 splice_locked (at, distance, exclude);
	 }
 }

 void
 Playlist::possibly_splice_unlocked (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude)
 {
	 if (_splicing || in_set_state) {
		 /* don't respond to splicing moves or state setting */
		 return;
	 }

	 if (_edit_mode == Splice) {
		 splice_unlocked (at, distance, exclude);
	 }
 }

 void
 Playlist::splice_locked (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude)
 {
	 {
		 RegionLock rl (this);
		 core_splice (at, distance, exclude);
	 }
 }

 void
 Playlist::splice_unlocked (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude)
 {
	 core_splice (at, distance, exclude);
 }

 void
 Playlist::core_splice (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude)
 {
	 _splicing = true;

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {

		 if (exclude && (*i) == exclude) {
			 continue;
		 }

		 if ((*i)->position() >= at) {
			 framepos_t new_pos = (*i)->position() + distance;
			 if (new_pos < 0) {
				 new_pos = 0;
			 } else if (new_pos >= max_framepos - (*i)->length()) {
				 new_pos = max_framepos - (*i)->length();
			 }

			 (*i)->set_position (new_pos);
		 }
	 }

	 _splicing = false;

	 notify_contents_changed ();
 }

 void
 Playlist::region_bounds_changed (const PropertyChange& what_changed, boost::shared_ptr<Region> region)
 {
	 if (in_set_state || _splicing || _nudging || _shuffling) {
		 return;
	 }

	 if (what_changed.contains (Properties::position)) {

		 timestamp_layer_op (LayerOpBoundsChange, region);
		 
		 /* remove it from the list then add it back in
		    the right place again.
		 */

		 RegionSortByPosition cmp;

		 RegionList::iterator i = find (regions.begin(), regions.end(), region);

		 if (i == regions.end()) {
			 /* the region bounds are being modified but its not currently
			    in the region list. we will use its bounds correctly when/if
			    it is added
			 */
			 return;
		 }

		 regions.erase (i);
		 regions.insert (upper_bound (regions.begin(), regions.end(), region, cmp), region);
	 }

	 if (what_changed.contains (Properties::position) || what_changed.contains (Properties::length)) {

		 frameoffset_t delta = 0;

		 if (what_changed.contains (Properties::position)) {
			 delta = region->position() - region->last_position();
		 }

		 if (what_changed.contains (Properties::length)) {
			 delta += region->length() - region->last_length();
		 }

		 if (delta) {
			 possibly_splice (region->last_position() + region->last_length(), delta, region);
		 }

		 if (holding_state ()) {
			 pending_bounds.push_back (region);
		 } else {
			 notify_contents_changed ();
			 relayer (region);
			 check_dependents (region, false);
		 }
	 }
 }

 void
 Playlist::region_changed_proxy (const PropertyChange& what_changed, boost::weak_ptr<Region> weak_region)
 {
	 boost::shared_ptr<Region> region (weak_region.lock());

	 if (!region) {
		 return;
	 }

	 /* this makes a virtual call to the right kind of playlist ... */

	 region_changed (what_changed, region);
 }

 bool
 Playlist::region_changed (const PropertyChange& what_changed, boost::shared_ptr<Region> region)
 {
	 PropertyChange our_interests;
	 PropertyChange bounds;
	 PropertyChange pos_and_length;
	 bool save = false;

	 if (in_set_state || in_flush) {
		 return false;
	 }

	 our_interests.add (Properties::muted);
	 our_interests.add (Properties::layer);
	 our_interests.add (Properties::opaque);

	 bounds.add (Properties::start);
	 bounds.add (Properties::position);
	 bounds.add (Properties::length);

	 pos_and_length.add (Properties::position);
	 pos_and_length.add (Properties::length);

	 if (what_changed.contains (bounds)) {
		 region_bounds_changed (what_changed, region);
		 save = !(_splicing || _nudging);
	 }

	 if (what_changed.contains (our_interests) && !what_changed.contains (pos_and_length)) {
		 check_dependents (region, false);
	 }

	 if (what_changed.contains (Properties::position) && !what_changed.contains (Properties::length)) {
		 notify_region_moved (region);
	 } else if (!what_changed.contains (Properties::position) && what_changed.contains (Properties::length)) {
		 notify_region_end_trimmed (region);
	 } else if (what_changed.contains (Properties::position) && what_changed.contains (Properties::length)) {
		 notify_region_start_trimmed (region);
	 }

	 if (what_changed.contains (our_interests)) {
		 save = true;
	 }

	 return save;
 }

 void
 Playlist::drop_regions ()
 {
	 RegionLock rl (this);
	 regions.clear ();
	 all_regions.clear ();
 }

 void
 Playlist::sync_all_regions_with_regions ()
 {
	 RegionLock rl (this);

	 all_regions.clear ();

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		 all_regions.insert (*i);
	 }
 }

 void
 Playlist::clear (bool with_signals)
 {
	 {
		 RegionLock rl (this);

		 region_state_changed_connections.drop_connections ();

		 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
			 pending_removes.insert (*i);
		 }

		 regions.clear ();

		 for (set<boost::shared_ptr<Region> >::iterator s = pending_removes.begin(); s != pending_removes.end(); ++s) {
			 remove_dependents (*s);
		 }
	 }

	 if (with_signals) {

		 for (set<boost::shared_ptr<Region> >::iterator s = pending_removes.begin(); s != pending_removes.end(); ++s) {
			 RegionRemoved (boost::weak_ptr<Region> (*s)); /* EMIT SIGNAL */
		 }

		 pending_removes.clear ();
		 pending_contents_change = false;
		 ContentsChanged ();
	 }

 }

 /***********************************************************************
  FINDING THINGS
  **********************************************************************/

 Playlist::RegionList *
 Playlist::regions_at (framepos_t frame)

 {
	 RegionLock rlock (this);
	 return find_regions_at (frame);
 }

 uint32_t
 Playlist::count_regions_at (framepos_t frame) const
 {
	 RegionLock rlock (const_cast<Playlist*>(this));
	 uint32_t cnt = 0;

	 for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		 if ((*i)->covers (frame)) {
			 cnt++;
		 }
	 }

	 return cnt;
 }

 boost::shared_ptr<Region>
 Playlist::top_region_at (framepos_t frame)

 {
	 RegionLock rlock (this);
	 RegionList *rlist = find_regions_at (frame);
	 boost::shared_ptr<Region> region;

	 if (rlist->size()) {
		 RegionSortByLayer cmp;
		 rlist->sort (cmp);
		 region = rlist->back();
	 }

	 delete rlist;
	 return region;
 }

 boost::shared_ptr<Region>
 Playlist::top_unmuted_region_at (framepos_t frame)

 {
	 RegionLock rlock (this);
	 RegionList *rlist = find_regions_at (frame);

	 for (RegionList::iterator i = rlist->begin(); i != rlist->end(); ) {

		 RegionList::iterator tmp = i;
		 ++tmp;

		 if ((*i)->muted()) {
			 rlist->erase (i);
		 }

		 i = tmp;
	 }

	 boost::shared_ptr<Region> region;

	 if (rlist->size()) {
		 RegionSortByLayer cmp;
		 rlist->sort (cmp);
		 region = rlist->back();
	 }

	 delete rlist;
	 return region;
 }

 Playlist::RegionList*
 Playlist::regions_to_read (framepos_t start, framepos_t end)
 {
	 /* Caller must hold lock */

	 RegionList covering;
	 set<framepos_t> to_check;
	 set<boost::shared_ptr<Region> > unique;

	 to_check.insert (start);
	 to_check.insert (end);

	 DEBUG_TRACE (DEBUG::AudioPlayback, ">>>>> REGIONS TO READ\n");

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {

		 /* find all/any regions that span start+end */

		 switch ((*i)->coverage (start, end)) {
		 case OverlapNone:
			 break;

		 case OverlapInternal:
			 covering.push_back (*i);
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("toread: will cover %1 (OInternal)\n", (*i)->name()));
			 break;

		 case OverlapStart:
			 to_check.insert ((*i)->position());
			 if ((*i)->position() != 0) {
				 to_check.insert ((*i)->position()-1);
			 }
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("toread: will check %1 for %2\n", (*i)->position(), (*i)->name()));
			 covering.push_back (*i);
			 break;

		 case OverlapEnd:
			 to_check.insert ((*i)->last_frame());
			 to_check.insert ((*i)->last_frame()+1);
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("toread: will cover %1 (OEnd)\n", (*i)->name()));
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("\ttoread: will check %1 for %2\n", (*i)->last_frame(), (*i)->name()));
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("\ttoread: will check %1 for %2\n", (*i)->last_frame(), (*i)->name()));
			 covering.push_back (*i);
			 break;

		 case OverlapExternal:
			 covering.push_back (*i);
			 to_check.insert ((*i)->position());
			 if ((*i)->position() != 0) {
				 to_check.insert ((*i)->position()-1);
			 }
			 to_check.insert ((*i)->last_frame());
			 to_check.insert ((*i)->last_frame()+1);
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("toread: will cover %1 (OExt)\n", (*i)->name()));
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("\ttoread: will check %1 for %2\n", (*i)->position(), (*i)->name()));
			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("\ttoread: will check %1 for %2\n", (*i)->last_frame(), (*i)->name()));
			 break;
		 }

		 /* don't go too far */

		 if ((*i)->position() > end) {
			 break;
		 }
	 }

	 RegionList* rlist = new RegionList;

	 /* find all the regions that cover each position .... */

	 if (covering.size() == 1) {

		 rlist->push_back (covering.front());
		 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("Just one covering region (%1)\n", covering.front()->name()));

	 } else {

		 RegionList here;
		 for (set<framepos_t>::iterator t = to_check.begin(); t != to_check.end(); ++t) {

			 here.clear ();

			 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("++++ Considering %1\n", *t));

			 for (RegionList::iterator x = covering.begin(); x != covering.end(); ++x) {

				 if ((*x)->covers (*t)) {
					 here.push_back (*x);
					 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("region %1 covers %2\n",
											    (*x)->name(),
											    (*t)));
				 } else {
					 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("region %1 does NOT covers %2\n",
											    (*x)->name(),
											    (*t)));
				 }

			 }

			 RegionSortByLayer cmp;
			 here.sort (cmp);

			 /* ... and get the top/transparent regions at "here" */

			 for (RegionList::reverse_iterator c = here.rbegin(); c != here.rend(); ++c) {

				 unique.insert (*c);

				 if ((*c)->opaque()) {

					 /* the other regions at this position are hidden by this one */
					 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("%1 is opaque, ignore all others\n",
											    (*c)->name()));
					 break;
				 }
			 }
		 }

		 for (set<boost::shared_ptr<Region> >::iterator s = unique.begin(); s != unique.end(); ++s) {
			 rlist->push_back (*s);
		 }

		 if (rlist->size() > 1) {
			 /* now sort by time order */

			 RegionSortByPosition cmp;
			 rlist->sort (cmp);
		 }
	 }

	 DEBUG_TRACE (DEBUG::AudioPlayback, string_compose ("<<<<< REGIONS TO READ returns %1\n", rlist->size()));

	 return rlist;
 }

 Playlist::RegionList *
 Playlist::find_regions_at (framepos_t frame)
 {
	 /* Caller must hold lock */

	 RegionList *rlist = new RegionList;

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		 if ((*i)->covers (frame)) {
			 rlist->push_back (*i);
		 }
	 }

	 return rlist;
 }

 Playlist::RegionList *
 Playlist::regions_touched (framepos_t start, framepos_t end)
 {
	 RegionLock rlock (this);
	 RegionList *rlist = new RegionList;

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		 if ((*i)->coverage (start, end) != OverlapNone) {
			 rlist->push_back (*i);
		 }
	 }

	 return rlist;
 }

 framepos_t
 Playlist::find_next_transient (framepos_t from, int dir)
 {
	 RegionLock rlock (this);
	 AnalysisFeatureList points;
	 AnalysisFeatureList these_points;

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		 if (dir > 0) {
			 if ((*i)->last_frame() < from) {
				 continue;
			 }
		 } else {
			 if ((*i)->first_frame() > from) {
				 continue;
			 }
		 }

		 (*i)->get_transients (these_points);

		 /* add first frame, just, err, because */

		 these_points.push_back ((*i)->first_frame());

		 points.insert (points.end(), these_points.begin(), these_points.end());
		 these_points.clear ();
	 }

	 if (points.empty()) {
		 return -1;
	 }

	 TransientDetector::cleanup_transients (points, _session.frame_rate(), 3.0);
	 bool reached = false;

	 if (dir > 0) {
		 for (AnalysisFeatureList::iterator x = points.begin(); x != points.end(); ++x) {
			 if ((*x) >= from) {
				 reached = true;
			 }

			 if (reached && (*x) > from) {
				 return *x;
			 }
		 }
	 } else {
		 for (AnalysisFeatureList::reverse_iterator x = points.rbegin(); x != points.rend(); ++x) {
			 if ((*x) <= from) {
				 reached = true;
			 }

			 if (reached && (*x) < from) {
				 return *x;
			 }
		 }
	 }

	 return -1;
 }

 boost::shared_ptr<Region>
 Playlist::find_next_region (framepos_t frame, RegionPoint point, int dir)
 {
	 RegionLock rlock (this);
	 boost::shared_ptr<Region> ret;
	 framepos_t closest = max_framepos;

	 bool end_iter = false;

	 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {

		 if(end_iter) break;

		 frameoffset_t distance;
		 boost::shared_ptr<Region> r = (*i);
		 framepos_t pos = 0;

		 switch (point) {
		 case Start:
			 pos = r->first_frame ();
			 break;
		 case End:
			 pos = r->last_frame ();
			 break;
		 case SyncPoint:
			 pos = r->sync_position ();
			 break;
		 }

		 switch (dir) {
		 case 1: /* forwards */

			 if (pos > frame) {
				 if ((distance = pos - frame) < closest) {
					 closest = distance;
					 ret = r;
					 end_iter = true;
				 }
			 }

			 break;

		 default: /* backwards */

			 if (pos < frame) {
				 if ((distance = frame - pos) < closest) {
					 closest = distance;
					 ret = r;
				 }
			 }
			 else {
				 end_iter = true;
			 }

			 break;
		 }
	 }

	 return ret;
 }

 framepos_t
 Playlist::find_next_region_boundary (framepos_t frame, int dir)
 {
	 RegionLock rlock (this);

	 framepos_t closest = max_framepos;
	 framepos_t ret = -1;

	 if (dir > 0) {

		 for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {

			 boost::shared_ptr<Region> r = (*i);
			 frameoffset_t distance;

			 if (r->first_frame() > frame) {

				 distance = r->first_frame() - frame;

				 if (distance < closest) {
					 ret = r->first_frame();
					 closest = distance;
				 }
			 }

			 if (r->last_frame () > frame) {

				 distance = r->last_frame () - frame;

				 if (distance < closest) {
					 ret = r->last_frame ();
					 closest = distance;
				 }
			 }
		 }

	 } else {

		 for (RegionList::reverse_iterator i = regions.rbegin(); i != regions.rend(); ++i) {

			 boost::shared_ptr<Region> r = (*i);
			 frameoffset_t distance;

			 if (r->last_frame() < frame) {

				 distance = frame - r->last_frame();

				 if (distance < closest) {
					 ret = r->last_frame();
					 closest = distance;
				 }
			 }

			 if (r->first_frame() < frame) {

				 distance = frame - r->first_frame();

				 if (distance < closest) {
					 ret = r->first_frame();
					 closest = distance;
				 }
			 }
		 }
	 }

	 return ret;
 }


 /***********************************************************************/




 void
 Playlist::mark_session_dirty ()
 {
	 if (!in_set_state && !holding_state ()) {
		 _session.set_dirty();
	 }
 }

 void
 Playlist::rdiff (vector<Command*>& cmds) const
 {
	 RegionLock rlock (const_cast<Playlist *> (this));
	 Stateful::rdiff (cmds);
 }

 void
 Playlist::clear_owned_changes ()
 {
	 RegionLock rlock (this);
	 Stateful::clear_owned_changes ();
 }

 void
 Playlist::update (const RegionListProperty::ChangeRecord& change)
 {
	 DEBUG_TRACE (DEBUG::Properties, string_compose ("Playlist %1 updates from a change record with %2 adds %3 removes\n",
							 name(), change.added.size(), change.removed.size()));

	 freeze ();
	 /* add the added regions */
	 for (RegionListProperty::ChangeContainer::iterator i = change.added.begin(); i != change.added.end(); ++i) {
		 add_region ((*i), (*i)->position());
	 }
	 /* remove the removed regions */
	 for (RegionListProperty::ChangeContainer::iterator i = change.removed.begin(); i != change.removed.end(); ++i) {
		 remove_region (*i);
	 }

	 thaw ();
 }

 int
 Playlist::set_state (const XMLNode& node, int version)
 {
	 XMLNode *child;
	 XMLNodeList nlist;
	 XMLNodeConstIterator niter;
	 XMLPropertyList plist;
	 XMLPropertyConstIterator piter;
	 XMLProperty *prop;
	 boost::shared_ptr<Region> region;
	 string region_name;
	 bool seen_region_nodes = false;
	 int ret = 0;

	 in_set_state++;

	 if (node.name() != "Playlist") {
		 in_set_state--;
		 return -1;
	 }

	 suspend_relayer ();
	 freeze ();

	 plist = node.properties();

	 set_id (node);

	 for (piter = plist.begin(); piter != plist.end(); ++piter) {

		 prop = *piter;

		 if (prop->name() == X_("name")) {
			 _name = prop->value();
			 _set_sort_id ();
		 } else if (prop->name() == X_("orig-diskstream-id")) {
			 /* XXX legacy session: fix up later */
			 _orig_track_id = prop->value ();
		 } else if (prop->name() == X_("orig-track-id")) {
			 _orig_track_id = prop->value ();
		 } else if (prop->name() == X_("frozen")) {
			 _frozen = string_is_affirmative (prop->value());
		 } else if (prop->name() == X_("combine-ops")) {
			 _combine_ops = atoi (prop->value());
		 }
	 }

	 clear (true);

	 nlist = node.children();

	 for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		 child = *niter;

		 if (child->name() == "Region") {

			 seen_region_nodes = true;

			 if ((prop = child->property ("id")) == 0) {
				 error << _("region state node has no ID, ignored") << endmsg;
				 continue;
			 }

			 ID id = prop->value ();

			 if ((region = region_by_id (id))) {

				 region->suspend_property_changes ();

				 if (region->set_state (*child, version)) {
					 region->resume_property_changes ();
					 continue;
				 }

			 } else if ((region = RegionFactory::create (_session, *child, true)) != 0) {
				 region->suspend_property_changes ();
			 } else {
				 error << _("Playlist: cannot create region from XML") << endmsg;
				return -1;
			}

			add_region (region, region->position(), 1.0);

			region->resume_property_changes ();

		}
	}

	if (seen_region_nodes && regions.empty()) {
		ret = -1;
	} else {

		/* update dependents, which was not done during add_region_internal
		   due to in_set_state being true
		*/
		
		for (RegionList::iterator r = regions.begin(); r != regions.end(); ++r) {
			check_dependents (*r, false);
		}
	}
		
	thaw ();
	notify_contents_changed ();
	resume_relayer ();

	in_set_state--;
	first_set_state = false;
	return ret;
}

XMLNode&
Playlist::get_state()
{
	return state (true);
}

XMLNode&
Playlist::get_template()
{
	return state (false);
}

/** @param full_state true to include regions in the returned state, otherwise false.
 */
XMLNode&
Playlist::state (bool full_state)
{
	XMLNode *node = new XMLNode (X_("Playlist"));
	char buf[64];

	node->add_property (X_("id"), id().to_s());
	node->add_property (X_("name"), _name);
	node->add_property (X_("type"), _type.to_string());

	_orig_track_id.print (buf, sizeof (buf));
	node->add_property (X_("orig-track-id"), buf);
	node->add_property (X_("frozen"), _frozen ? "yes" : "no");

	if (full_state) {
		RegionLock rlock (this, false);

		snprintf (buf, sizeof (buf), "%u", _combine_ops);
		node->add_property ("combine-ops", buf);

		for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
			node->add_child_nocopy ((*i)->get_state());
		}
	}

	if (_extra_xml) {
		node->add_child_copy (*_extra_xml);
	}

	return *node;
}

bool
Playlist::empty() const
{
	RegionLock rlock (const_cast<Playlist *>(this), false);
	return regions.empty();
}

uint32_t
Playlist::n_regions() const
{
	RegionLock rlock (const_cast<Playlist *>(this), false);
	return regions.size();
}

pair<framepos_t, framepos_t>
Playlist::get_extent () const
{
	RegionLock rlock (const_cast<Playlist *>(this), false);
	return _get_extent ();
}

pair<framepos_t, framepos_t>
Playlist::_get_extent () const
{
	pair<framepos_t, framepos_t> ext (max_framepos, 0);

	if (regions.empty()) {
		ext.first = 0;
		return ext;
	}

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		pair<framepos_t, framepos_t> const e ((*i)->position(), (*i)->position() + (*i)->length());
		if (e.first < ext.first) {
			ext.first = e.first;
		}
		if (e.second > ext.second) {
			ext.second = e.second;
		}
	}

	return ext;
}

string
Playlist::bump_name (string name, Session &session)
{
	string newname = name;

	do {
		newname = bump_name_once (newname, '.');
	} while (session.playlists->by_name (newname)!=NULL);

	return newname;
}


layer_t
Playlist::top_layer() const
{
	RegionLock rlock (const_cast<Playlist *> (this));
	layer_t top = 0;

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		top = max (top, (*i)->layer());
	}
	return top;
}

void
Playlist::set_edit_mode (EditMode mode)
{
	_edit_mode = mode;
}

/** Relayer a region.  See the other relayer() methods for commentary. */
void
Playlist::relayer (boost::shared_ptr<Region> region)
{
	if (_relayer_suspended) {
		return;
	}
	
	RegionList r;
	r.push_back (region);
	relayer (r);
}

Playlist::TemporaryLayers
Playlist::compute_temporary_layers (RegionList const & relayer_regions)
{
	TemporaryLayers temporary_layers;
	OverlapCache cache (this);

	for (RegionList::const_iterator i = relayer_regions.begin(); i != relayer_regions.end(); ++i) {

		DEBUG_TRACE (DEBUG::Layering, string_compose ("Compute temporary layer for %1\n", (*i)->name()));
	
		/* current_overlaps: regions that overlap *i now */
		RegionList current_overlaps = cache.get ((*i)->bounds ());
		current_overlaps.remove (*i);

		DEBUG_TRACE (DEBUG::Layering, "Current overlaps:\n");
		for (RegionList::iterator j = current_overlaps.begin(); j != current_overlaps.end(); ++j) {
			DEBUG_TRACE (DEBUG::Layering, string_compose ("\t%1\n", (*j)->name()));
		}
		
		/* overlaps_to_preserve: regions that overlap *i now, but which aren't being
		   worked on during this relayer: these will have their relationship with
		   *i preserved.
		*/
		RegionList overlaps_to_preserve;

		/* overlaps_to_check: regions that overlap *i now, and must be checked to
		   see if *i is still on the correct layer with respect to them (according
		   to current layering rules).
		*/
		RegionList overlaps_to_check;

		if (_session.config.get_relayer_on_all_edits () || (*i)->last_layer_op (LayerOpAdd) > (*i)->last_layer_op (LayerOpBoundsChange)) {
			/* We're configured to relayer on all edits, or this region has had
			   no edit since it was added to the playlist, so we're relayering
			   the whole lot; in this case there are no `overlaps_to_preserve'.
			*/
			overlaps_to_check = current_overlaps;
		} else {
			/* We're only relayering new overlaps; find them */
			RegionList last_overlaps = cache.get ((*i)->last_relayer_bounds ());
			last_overlaps.remove (*i);
			for (RegionList::const_iterator j = current_overlaps.begin(); j != current_overlaps.end(); ++j) {
				if (find (last_overlaps.begin(), last_overlaps.end(), *j) == last_overlaps.end()) {
					/* This is a new overlap, which must be checked */
					overlaps_to_check.push_back (*j);
				} else {
					/* This is an existing overlap, which must be preserved */
					overlaps_to_preserve.push_back (*j);
				}
			}
		}

		if (overlaps_to_check.empty ()) {
			/* There are no overlaps to check, so just leave everything as it is */
			continue;
		}

		DEBUG_TRACE (DEBUG::Layering, "Overlaps to check:\n");
		for (RegionList::iterator j = overlaps_to_check.begin(); j != overlaps_to_check.end(); ++j) {
			DEBUG_TRACE (DEBUG::Layering, string_compose ("\t%1\n", (*j)->name()));
		}
		
		/* Put *i on our overlaps_to_check_list */
		overlaps_to_check.push_back (*i);

		/* And sort it according to the current layer model */
		switch (_session.config.get_layer_model()) {
		case LaterHigher:
			overlaps_to_check.sort (RegionSortByPosition ());
			break;
		case AddOrBoundsChangeHigher:
			overlaps_to_check.sort (RegionSortByAddOrBounds ());
			break;
		case AddHigher:
			overlaps_to_check.sort (RegionSortByAdd ());
			break;
		}

		/* Now find *i in our overlaps_to_check list; within this list it will be in the
		   right place wrt the current layering rules, so we can work out the layers of the
		   nearest regions below and above.
		*/
		double previous_layer = -DBL_MAX;
		double next_layer = DBL_MAX;
		RegionList::const_iterator j = overlaps_to_check.begin();
		while (*j != *i) {
			previous_layer = temporary_layers.get (*j);
			++j;
		}

		/* we must have found *i */
		assert (j != overlaps_to_check.end ());

		++j;
		if (j != overlaps_to_check.end ()) {
			next_layer = temporary_layers.get (*j);
		}

		if (next_layer < previous_layer) {
			/* If this happens, it means that it's impossible to put *i between overlaps_to_check
			   in a way that satisfies the current layering rule.  So we'll punt and put *i
			   above previous_layer.
			*/
			next_layer = DBL_MAX;
		}

		/* Now we know where *i and overlaps_to_preserve should go: between previous_layer and
		   next_layer.
		*/

		DEBUG_TRACE (DEBUG::Layering, string_compose ("%1 and deps need to go between %2 and %3\n", (*i)->name(), previous_layer, next_layer));

		/* Recurse into overlaps_to_preserve to find dependents */
		RegionList recursed_overlaps_to_preserve;
		
		for (RegionList::const_iterator k = overlaps_to_preserve.begin(); k != overlaps_to_preserve.end(); ++k) {
			recursed_overlaps_to_preserve.push_back (*k);
			RegionList touched = recursive_regions_touched (*k, cache, *i);
			for (RegionList::iterator m = touched.begin(); m != touched.end(); ++m) {
				if (find (recursed_overlaps_to_preserve.begin(), recursed_overlaps_to_preserve.end(), *m) == recursed_overlaps_to_preserve.end()) {
					recursed_overlaps_to_preserve.push_back (*m);
				}
			}
		}

		/* Put *i into the overlaps_to_preserve list */
		recursed_overlaps_to_preserve.push_back (*i);

		/* Sort it by layer, so that we preserve layering */
		recursed_overlaps_to_preserve.sort (SortByTemporaryLayer (temporary_layers));

		/* Divide available space up into chunks so that we can relayer everything in that space */
		double const space = (next_layer - previous_layer) / (recursed_overlaps_to_preserve.size() + 1);

		/* And relayer */
		int m = 1;
		for (RegionList::const_iterator k = recursed_overlaps_to_preserve.begin(); k != recursed_overlaps_to_preserve.end(); ++k) {
			temporary_layers.set (*k, previous_layer + space * m);
			++m;
		}
	}

	return temporary_layers;
}

/** Take a list of temporary layer indices and set up the layers of all regions
 *  based on them.
 */
void
Playlist::commit_temporary_layers (TemporaryLayers const & temporary_layers)
{
	/* Sort all the playlist's regions by layer, ascending */
	RegionList all_regions = regions.rlist ();
	all_regions.sort (SortByTemporaryLayer (temporary_layers));

	DEBUG_TRACE (DEBUG::Layering, "Commit layering:\n");

	for (RegionList::iterator i = all_regions.begin(); i != all_regions.end(); ++i) {

		/* Go through the regions that we have already layered and hence work
		   out the maximum layer index that is in used at some point during
		   region *i.
		*/
		
		layer_t max_layer_here = 0;
		bool have_overlap = false;
		for (RegionList::iterator j = all_regions.begin(); j != i; ++j) {
			if ((*j)->overlap_equivalent (*i)) {
				max_layer_here = max ((*j)->layer (), max_layer_here);
				have_overlap = true;
			}
		}

		if (have_overlap) {
			/* *i overlaps something, so put it on the next available layer */
			(*i)->set_layer (max_layer_here + 1);
		} else {
			/* no overlap, so put on the bottom layer */
			(*i)->set_layer (0);
		}
		
		DEBUG_TRACE (DEBUG::Layering, string_compose ("\t%1 temporary %2 committed %3\n", (*i)->name(), temporary_layers.get (*i), (*i)->layer()));
	}

	notify_layering_changed ();
}

/** Relayer a list of regions.
 *
 *  Taking each region R in turn, this method examines the regions O that overlap R in time.
 *  If the session configuration option "relayer-on-all-moves" is false, we reduce O so that
 *  it contains only those regions with which new overlaps have been formed since the last
 *  relayer.
 *
 *  We then change the layer of R and its indirect overlaps so that R meets the current
 *  Session layer model with respect to O.  See doc/layering.
 */

void
Playlist::relayer (RegionList const & relayer_regions)
{
	if (_relayer_suspended) {
		return;
	}

	/* We do this in two parts: first; compute `temporary layer' indices for
	   regions on the playlist.  These are (possibly) fractional indices, which
	   are a convenient means of working with things when you want to insert layers
	   between others.
	*/

	TemporaryLayers temporary_layers = compute_temporary_layers (relayer_regions);

	/* Second, we fix up these temporary layers and `commit' them by writing
	   them to the regions involved.
	*/
	
	commit_temporary_layers (temporary_layers);
}

/** Put a region on some fractional layer and sort everything else out around it.
 *  This can be used to force a region into some layering; for example, calling
 *  this method with temporary_layer == -0.5 will put the region at the bottom of
 *  the stack.
 */

void
Playlist::relayer (boost::shared_ptr<Region> region, double temporary_layer)
{
	if (_relayer_suspended) {
		return;
	}

	TemporaryLayers t;
	t.set (region, temporary_layer);
	commit_temporary_layers (t);
}

void
Playlist::raise_region (boost::shared_ptr<Region> region)
{
	relayer (region, region->layer() + 1.5);
}

void
Playlist::lower_region (boost::shared_ptr<Region> region)
{
	relayer (region, region->layer() - 1.5);
}

void
Playlist::raise_region_to_top (boost::shared_ptr<Region> region)
{
	relayer (region, max_layer);
}

void
Playlist::lower_region_to_bottom (boost::shared_ptr<Region> region)
{
	relayer (region, -0.5);
}

void
Playlist::nudge_after (framepos_t start, framecnt_t distance, bool forwards)
{
	RegionList::iterator i;
	bool moved = false;

	_nudging = true;

	{
		RegionLock rlock (const_cast<Playlist *> (this));

		for (i = regions.begin(); i != regions.end(); ++i) {

			if ((*i)->position() >= start) {

				framepos_t new_pos;

				if (forwards) {

					if ((*i)->last_frame() > max_framepos - distance) {
						new_pos = max_framepos - (*i)->length();
					} else {
						new_pos = (*i)->position() + distance;
					}

				} else {

					if ((*i)->position() > distance) {
						new_pos = (*i)->position() - distance;
					} else {
						new_pos = 0;
					}
				}

				(*i)->set_position (new_pos);
				moved = true;
			}
		}
	}

	if (moved) {
		_nudging = false;
		notify_contents_changed ();
	}

}

bool
Playlist::uses_source (boost::shared_ptr<const Source> src) const
{
	RegionLock rlock (const_cast<Playlist*> (this));

	for (set<boost::shared_ptr<Region> >::iterator r = all_regions.begin(); r != all_regions.end(); ++r) {
		if ((*r)->uses_source (src)) {
			return true;
		}
	}

	return false;
}

boost::shared_ptr<Region>
Playlist::find_region (const ID& id) const
{
	RegionLock rlock (const_cast<Playlist*> (this));

	/* searches all regions currently in use by the playlist */

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		if ((*i)->id() == id) {
			return *i;
		}
	}

	return boost::shared_ptr<Region> ();
}

uint32_t
Playlist::region_use_count (boost::shared_ptr<Region> r) const
{
	RegionLock rlock (const_cast<Playlist*> (this));
	uint32_t cnt = 0;

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		if ((*i) == r) {
			cnt++;
		}
	}

	return cnt;
}

boost::shared_ptr<Region>
Playlist::region_by_id (const ID& id) const
{
	/* searches all regions ever added to this playlist */

	for (set<boost::shared_ptr<Region> >::iterator i = all_regions.begin(); i != all_regions.end(); ++i) {
		if ((*i)->id() == id) {
			return *i;
		}
	}
	return boost::shared_ptr<Region> ();
}

void
Playlist::dump () const
{
	boost::shared_ptr<Region> r;

	cerr << "Playlist \"" << _name << "\" " << endl
	     << regions.size() << " regions "
	     << endl;

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		r = *i;
		cerr << "  " << r->name() << " ["
		     << r->start() << "+" << r->length()
		     << "] at "
		     << r->position()
		     << " on layer "
		     << r->layer ()
		     << endl;
	}
}

void
Playlist::set_frozen (bool yn)
{
	_frozen = yn;
}

void
Playlist::timestamp_layer_op (LayerOp op, boost::shared_ptr<Region> region)
{
	region->set_last_layer_op (op, ++layer_op_counter);
}


/** Find the next or previous region after `region' (next if dir > 0, previous otherwise)
 *  and swap its position with `region'.
 */
void
Playlist::shuffle (boost::shared_ptr<Region> region, int dir)
{
	/* As regards layering, the calls we make to set_position() will
	   perform layering as if the regions had been moved, which I think
	   is about right.
	*/
	
	bool moved = false;

	if (region->locked()) {
		return;
	}

	_shuffling = true;

	{
		RegionLock rlock (const_cast<Playlist*> (this));


		if (dir > 0) {

			RegionList::iterator next;

			for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
				if ((*i) == region) {
					next = i;
					++next;

					if (next != regions.end()) {

						if ((*next)->locked()) {
							break;
						}

						framepos_t new_pos;

						if ((*next)->position() != region->last_frame() + 1) {
							/* they didn't used to touch, so after shuffle,
							   just have them swap positions.
							*/
							new_pos = (*next)->position();
						} else {
							/* they used to touch, so after shuffle,
							   make sure they still do. put the earlier
							   region where the later one will end after
							   it is moved.
							*/
							new_pos = region->position() + (*next)->length();
						}

						(*next)->set_position (region->position());
						region->set_position (new_pos);

						/* avoid a full sort */

						regions.erase (i); // removes the region from the list */
						next++;
						regions.insert (next, region); // adds it back after next

						moved = true;
					}
					break;
				}
			}
		} else {

			RegionList::iterator prev = regions.end();

			for (RegionList::iterator i = regions.begin(); i != regions.end(); prev = i, ++i) {
				if ((*i) == region) {

					if (prev != regions.end()) {

						if ((*prev)->locked()) {
							break;
						}

						framepos_t new_pos;
						if (region->position() != (*prev)->last_frame() + 1) {
							/* they didn't used to touch, so after shuffle,
							   just have them swap positions.
							*/
							new_pos = region->position();
						} else {
							/* they used to touch, so after shuffle,
							   make sure they still do. put the earlier
							   one where the later one will end after
							*/
							new_pos = (*prev)->position() + region->length();
						}

						region->set_position ((*prev)->position());
						(*prev)->set_position (new_pos);

						/* avoid a full sort */

						regions.erase (i); // remove region
						regions.insert (prev, region); // insert region before prev

						moved = true;
					}

					break;
				}
			}
		}
	}

	_shuffling = false;

	if (moved) {
		check_dependents (region, false);
		notify_contents_changed();
	}
}

bool
Playlist::region_is_shuffle_constrained (boost::shared_ptr<Region>)
{
	RegionLock rlock (const_cast<Playlist*> (this));

	if (regions.size() > 1) {
		return true;
	}

	return false;
}

void
Playlist::update_after_tempo_map_change ()
{
	RegionLock rlock (const_cast<Playlist*> (this));
	RegionList copy (regions.rlist());

	freeze ();

	for (RegionList::iterator i = copy.begin(); i != copy.end(); ++i) {
		(*i)->update_after_tempo_map_change ();
	}

	thaw ();
}

void
Playlist::foreach_region (boost::function<void(boost::shared_ptr<Region>)> s)
{
	RegionLock rl (this, false);
	for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		s (*i);
	}
}

bool
Playlist::has_region_at (framepos_t const p) const
{
	RegionLock (const_cast<Playlist *> (this));

	RegionList::const_iterator i = regions.begin ();
	while (i != regions.end() && !(*i)->covers (p)) {
		++i;
	}

	return (i != regions.end());
}

/** Remove any region that uses a given source */
void
Playlist::remove_region_by_source (boost::shared_ptr<Source> s)
{
	RegionLock rl (this);

	RegionList::iterator i = regions.begin();
	while (i != regions.end()) {
		RegionList::iterator j = i;
		++j;

		if ((*i)->uses_source (s)) {
			remove_region_internal (*i);
		}

		i = j;
	}
}

/** Look from a session frame time and find the start time of the next region
 *  which is on the top layer of this playlist.
 *  @param t Time to look from.
 *  @return Position of next top-layered region, or max_framepos if there isn't one.
 */
framepos_t
Playlist::find_next_top_layer_position (framepos_t t) const
{
	RegionLock rlock (const_cast<Playlist *> (this));

	layer_t const top = top_layer ();

	RegionList copy = regions.rlist ();
	copy.sort (RegionSortByPosition ());

	for (RegionList::const_iterator i = copy.begin(); i != copy.end(); ++i) {
		if ((*i)->position() >= t && (*i)->layer() == top) {
			return (*i)->position();
		}
	}

	return max_framepos;
}

boost::shared_ptr<Region>
Playlist::combine (const RegionList& r)
{
	PropertyList plist;
	uint32_t channels = 0;
	uint32_t layer = 0;
	framepos_t earliest_position = max_framepos;
	vector<TwoRegions> old_and_new_regions;
	vector<boost::shared_ptr<Region> > originals;
	vector<boost::shared_ptr<Region> > copies;
	string parent_name;
	string child_name;
	uint32_t max_level = 0;

	/* find the maximum depth of all the regions we're combining */

	for (RegionList::const_iterator i = r.begin(); i != r.end(); ++i) {
		max_level = max (max_level, (*i)->max_source_level());
	}

	parent_name = RegionFactory::compound_region_name (name(), combine_ops(), max_level, true);
	child_name = RegionFactory::compound_region_name (name(), combine_ops(), max_level, false);

	boost::shared_ptr<Playlist> pl = PlaylistFactory::create (_type, _session, parent_name, true);

	for (RegionList::const_iterator i = r.begin(); i != r.end(); ++i) {
		earliest_position = min (earliest_position, (*i)->position());
	}

	/* enable this so that we do not try to create xfades etc. as we add
	 * regions
	 */

	pl->in_partition = true;

	for (RegionList::const_iterator i = r.begin(); i != r.end(); ++i) {

		/* copy the region */

		boost::shared_ptr<Region> original_region = (*i);
		boost::shared_ptr<Region> copied_region = RegionFactory::create (original_region, false);

		old_and_new_regions.push_back (TwoRegions (original_region,copied_region));
		originals.push_back (original_region);
		copies.push_back (copied_region);

		RegionFactory::add_compound_association (original_region, copied_region);

		/* make position relative to zero */

		pl->add_region (copied_region, original_region->position() - earliest_position);

		/* use the maximum number of channels for any region */

		channels = max (channels, original_region->n_channels());

		/* it will go above the layer of the highest existing region */

		layer = max (layer, original_region->layer());
	}

	pl->in_partition = false;

	pre_combine (copies);

	/* add any dependent regions to the new playlist */

	copy_dependents (old_and_new_regions, pl.get());

	/* now create a new PlaylistSource for each channel in the new playlist */

	SourceList sources;
	pair<framepos_t,framepos_t> extent = pl->get_extent();

	for (uint32_t chn = 0; chn < channels; ++chn) {
		sources.push_back (SourceFactory::createFromPlaylist (_type, _session, pl, id(), parent_name, chn, 0, extent.second, false, false));

	}

	/* now a new whole-file region using the list of sources */

	plist.add (Properties::start, 0);
	plist.add (Properties::length, extent.second);
	plist.add (Properties::name, parent_name);
	plist.add (Properties::whole_file, true);

	boost::shared_ptr<Region> parent_region = RegionFactory::create (sources, plist, true);

	/* now the non-whole-file region that we will actually use in the
	 * playlist
	 */

	plist.clear ();
	plist.add (Properties::start, 0);
	plist.add (Properties::length, extent.second);
	plist.add (Properties::name, child_name);
	plist.add (Properties::layer, layer+1);

	boost::shared_ptr<Region> compound_region = RegionFactory::create (parent_region, plist, true);

	/* remove all the selected regions from the current playlist
	 */

	freeze ();

	for (RegionList::const_iterator i = r.begin(); i != r.end(); ++i) {
		remove_region (*i);
	}

	/* do type-specific stuff with the originals and the new compound
	   region
	*/

	post_combine (originals, compound_region);

	/* add the new region at the right location */

	add_region (compound_region, earliest_position);

	_combine_ops++;

	thaw ();

	return compound_region;
}

void
Playlist::uncombine (boost::shared_ptr<Region> target)
{
	boost::shared_ptr<PlaylistSource> pls;
	boost::shared_ptr<const Playlist> pl;
	vector<boost::shared_ptr<Region> > originals;
	vector<TwoRegions> old_and_new_regions;

	// (1) check that its really a compound region

	if ((pls = boost::dynamic_pointer_cast<PlaylistSource>(target->source (0))) == 0) {
		return;
	}

	pl = pls->playlist();

	framepos_t adjusted_start = 0; // gcc isn't smart enough
	framepos_t adjusted_end = 0;   // gcc isn't smart enough

	/* the leftmost (earliest) edge of the compound region
	   starts at zero in its source, or larger if it
	   has been trimmed or content-scrolled.

	   the rightmost (latest) edge of the compound region
	   relative to its source is the starting point plus
	   the length of the region.
	*/

	// (2) get all the original regions

	const RegionList& rl (pl->region_list().rlist());
	RegionFactory::CompoundAssociations& cassocs (RegionFactory::compound_associations());
	frameoffset_t move_offset = 0;

	/* there are two possibilities here:
	   1) the playlist that the playlist source was based on
	   is us, so just add the originals (which belonged to
	   us anyway) back in the right place.

	   2) the playlist that the playlist source was based on
	   is NOT us, so we need to make copies of each of
	   the original regions that we find, and add them
	   instead.
	*/
	bool same_playlist = (pls->original() == id());

	for (RegionList::const_iterator i = rl.begin(); i != rl.end(); ++i) {

		boost::shared_ptr<Region> current (*i);

		RegionFactory::CompoundAssociations::iterator ca = cassocs.find (*i);

		if (ca == cassocs.end()) {
			continue;
		}

		boost::shared_ptr<Region> original (ca->second);
		bool modified_region;

		if (i == rl.begin()) {
			move_offset = (target->position() - original->position()) - target->start();
			adjusted_start = original->position() + target->start();
			adjusted_end = adjusted_start + target->length();
		}

		if (!same_playlist) {
			framepos_t pos = original->position();
			/* make a copy, but don't announce it */
			original = RegionFactory::create (original, false);
			/* the pure copy constructor resets position() to zero,
			   so fix that up.
			*/
			original->set_position (pos);
		}

		/* check to see how the original region (in the
		 * playlist before compounding occured) overlaps
		 * with the new state of the compound region.
		 */

		original->clear_changes ();
		modified_region = false;

		switch (original->coverage (adjusted_start, adjusted_end)) {
		case OverlapNone:
			/* original region does not cover any part
			   of the current state of the compound region
			*/
			continue;

		case OverlapInternal:
			/* overlap is just a small piece inside the
			 * original so trim both ends
			 */
			original->trim_to (adjusted_start, adjusted_end - adjusted_start);
			modified_region = true;
			break;

		case OverlapExternal:
			/* overlap fully covers original, so leave it
			   as is
			*/
			break;

		case OverlapEnd:
			/* overlap starts within but covers end,
			   so trim the front of the region
			*/
			original->trim_front (adjusted_start);
			modified_region = true;
			break;

		case OverlapStart:
			/* overlap covers start but ends within, so
			 * trim the end of the region.
			 */
			original->trim_end (adjusted_end);
			modified_region = true;
			break;
		}

		if (move_offset) {
			/* fix the position to match any movement of the compound region.
			 */
			original->set_position (original->position() + move_offset);
			modified_region = true;
		}

		if (modified_region) {
			_session.add_command (new StatefulDiffCommand (original));
		}

		/* and add to the list of regions waiting to be
		 * re-inserted
		 */

		originals.push_back (original);
		old_and_new_regions.push_back (TwoRegions (*i, original));
	}

	pre_uncombine (originals, target);

	in_partition = true;
	freeze ();

	// (3) remove the compound region

	remove_region (target);

	// (4) add the constituent regions

	for (vector<boost::shared_ptr<Region> >::iterator i = originals.begin(); i != originals.end(); ++i) {
		add_region ((*i), (*i)->position());
	}

	/* now move dependent regions back from the compound to this playlist */

	pl->copy_dependents (old_and_new_regions, this);

	in_partition = false;
	thaw ();
}

uint32_t
Playlist::max_source_level () const
{
	RegionLock rlock (const_cast<Playlist *> (this));
	uint32_t lvl = 0;

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		lvl = max (lvl, (*i)->max_source_level());
	}

	return lvl;
}


uint32_t
Playlist::count_joined_regions () const
{
	RegionLock rlock (const_cast<Playlist *> (this));
	uint32_t cnt = 0;

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		if ((*i)->max_source_level() > 0) {
			cnt++;
		}
	}

	return cnt;
}

void
Playlist::set_orig_track_id (const PBD::ID& id)
{
	_orig_track_id = id;
}

/** Set the temporary layer for a region */
void
Playlist::TemporaryLayers::set (boost::shared_ptr<Region> r, double l)
{
	_map[r] = l;
}

/** Return the temporary layer for a region, if one has been specified
 *  to this TemporaryLayers object; if not return the region's current
 *  layer.
 */
double
Playlist::TemporaryLayers::get (boost::shared_ptr<Region> r) const
{
	Map::const_iterator i = _map.find (r);
	if (i != _map.end ()) {
		return i->second;
	}

	return double (r->layer ());
}

int const Playlist::OverlapCache::_divisions = 512;

/** Set up an OverlapCache for a playlist; the cache will only be valid until
 *  the Playlist is changed.
 */
Playlist::OverlapCache::OverlapCache (Playlist* playlist)
	: _range (0, 0)
{
	/* Find the start and end positions of the regions on this playlist */
	_range = Evoral::Range<framepos_t> (max_framepos, 0);
	RegionList const & rl = playlist->region_list().rlist ();
	for (RegionList::const_iterator i = rl.begin(); i != rl.end(); ++i) {
		Evoral::Range<framepos_t> const b = (*i)->bounds ();
		_range.from = min (_range.from, b.from);
		_range.to = max (_range.to, b.to);
	}

	/* Hence the size of each time divison */
	_division_size = (_range.to - _range.from) / double (_divisions);

	_cache.resize (_divisions);

	/* Build the cache */
	for (RegionList::const_iterator i = rl.begin(); i != rl.end(); ++i) {
		pair<int, int> ind = cache_indices ((*i)->bounds ());
		for (int j = ind.first; j < ind.second; ++j) {
			_cache[j].push_back (*i);
		}
	}
}

/** @param range Range, in frames.
 *  @return From and to cache indices for  (to is exclusive).
 */
pair<int, int>
Playlist::OverlapCache::cache_indices (Evoral::Range<framepos_t> range) const
{
	range.from = max (range.from, _range.from);
	range.to = min (range.to, _range.to);
	
	pair<int, int> const p = make_pair (
		floor ((range.from - _range.from) / _division_size),
		ceil ((range.to - _range.from) / _division_size)
		);

	assert (p.first >= 0);
	assert (p.second <= _divisions);

	return p;
}

/** Return the regions that overlap a given range.  The returned list
 *  is not guaranteed to be in the same order as the Playlist that it was
 *  generated from.
 */
Playlist::RegionList
Playlist::OverlapCache::get (Evoral::Range<framepos_t> range) const
{
	if (_range.from == max_framepos) {
		return RegionList ();
	}
	
	RegionList r;

	pair<int, int> ind = cache_indices (range);
	for (int i = ind.first; i < ind.second; ++i) {
		for (RegionList::const_iterator j = _cache[i].begin(); j != _cache[i].end(); ++j) {
			if ((*j)->coverage (range.from, range.to) != OverlapNone) {
				r.push_back (*j);
			}
		}
	}

	r.sort ();
	r.unique ();

	return r;
}

void
Playlist::suspend_relayer ()
{
	_relayer_suspended = true;
}

void
Playlist::resume_relayer ()
{
	_relayer_suspended = false;
}

/** Examine a region and return regions which overlap it, and also those which overlap those which overlap etc.
 *  @param ignore Optional region which should be treated as if it doesn't exist (ie not returned in the list,
 *  and not recursed into).
 */
Playlist::RegionList
Playlist::recursive_regions_touched (boost::shared_ptr<Region> region, OverlapCache const & cache, boost::shared_ptr<Region> ignore) const
{
	RegionList touched;
	recursive_regions_touched_sub (region, cache, ignore, touched);

	touched.remove (region);
	return touched;
}

/** Recursive sub-routine of recursive_regions_touched */
void
Playlist::recursive_regions_touched_sub (
	boost::shared_ptr<Region> region, OverlapCache const & cache, boost::shared_ptr<Region> ignore, RegionList & touched
	) const
{
	RegionList r = cache.get (region->bounds ());
	for (RegionList::iterator i = r.begin(); i != r.end(); ++i) {
		if (find (touched.begin(), touched.end(), *i) == touched.end() && *i != ignore) {
			touched.push_back (*i);
			recursive_regions_touched_sub (*i, cache, ignore, touched);
		}
	}
}

