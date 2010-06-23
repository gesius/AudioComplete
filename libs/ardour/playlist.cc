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

#define __STDC_LIMIT_MACROS
#include <stdint.h>

#include <set>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <climits>

#include <boost/lexical_cast.hpp>

#include "pbd/failed_constructor.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "ardour/debug.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/playlist_factory.h"
#include "ardour/transient_detector.h"
#include "ardour/session_playlists.h"

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

struct RegionSortByLayer {
    bool operator() (boost::shared_ptr<Region> a, boost::shared_ptr<Region> b) {
	    return a->layer() < b->layer();
    }
};

struct RegionSortByLayerWithPending {
	bool operator () (boost::shared_ptr<Region> a, boost::shared_ptr<Region> b) {

		double p = a->layer ();
		if (a->pending_explicit_relayer()) {
			p += 0.5;
		}

		double q = b->layer ();
		if (b->pending_explicit_relayer()) {
			q += 0.5;
		}

		return p < q;
	}
};

struct RegionSortByPosition {
    bool operator() (boost::shared_ptr<Region> a, boost::shared_ptr<Region> b) {
	    return a->position() < b->position();
    }
};

struct RegionSortByLastLayerOp {
    bool operator() (boost::shared_ptr<Region> a, boost::shared_ptr<Region> b) {
	    return a->last_layer_op() < b->last_layer_op();
    }
};

void
Playlist::make_property_quarks ()
{
        Properties::regions.property_id = g_quark_from_static_string (X_("regions"));
        DEBUG_TRACE (DEBUG::Properties, string_compose ("quark for regions = %1\n",         Properties::regions.property_id));
}

RegionListProperty::RegionListProperty (Playlist& pl)
        : SequenceProperty<std::list<boost::shared_ptr<Region> > > (Properties::regions.property_id, boost::bind (&Playlist::update, &pl, _1))
        , _playlist (pl)
{
}

boost::shared_ptr<Region>
RegionListProperty::lookup_id (const ID& id)
{
        boost::shared_ptr<Region> ret =  _playlist.region_by_id (id);
        
        if (!ret) {
                ret = RegionFactory::region_by_id (id);
        }

        return ret;
}

RegionListProperty*
RegionListProperty::copy_for_history () const
{
        RegionListProperty* copy = new RegionListProperty (_playlist);
        /* this is all we need */
        copy->_change = _change;
        return copy;
}

void 
RegionListProperty::diff (PropertyList& undo, PropertyList& redo, Command* cmd) const
{
        if (changed()) {
		/* list of the removed/added regions since clear_history() was last called */
                RegionListProperty* a = copy_for_history ();

		/* the same list, but with removed/added lists swapped (for undo purposes) */
                RegionListProperty* b = copy_for_history ();
                b->invert_changes ();

                if (cmd) {
                        /* whenever one of the regions emits DropReferences, make sure
                           that the Destructible we've been told to notify hears about
                           it. the Destructible is likely to be the Command being built
                           with this diff().
                        */
                        
                        for (set<boost::shared_ptr<Region> >::iterator i = a->change().added.begin(); i != a->change().added.end(); ++i) {
                                (*i)->DropReferences.connect_same_thread (*cmd, boost::bind (&Destructible::drop_references, cmd));
                        }
                }

                undo.add (b);
                redo.add (a);
        }
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
	, _orig_diskstream_id (other->_orig_diskstream_id)
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
	_read_data_count = 0;
	_frozen = other->_frozen;

	layer_op_counter = other->layer_op_counter;
	freeze_length = other->freeze_length;
}

Playlist::Playlist (boost::shared_ptr<const Playlist> other, framepos_t start, framecnt_t cnt, string str, bool hide)
	: SessionObject(other->_session, str)
        , regions (*this)
	, _type(other->_type)
	, _orig_diskstream_id (other->_orig_diskstream_id)
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

	/* this constructor does NOT notify others (session) */
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
		newlist.push_back (RegionFactory::RegionFactory::create (*i));
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
	pending_length = false;
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
	_read_data_count = 0;
	_frozen = false;
	layer_op_counter = 0;
	freeze_length = 0;
	_explicit_relayering = false;

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
	thaw ();
        in_update = false;
}

void
Playlist::freeze ()
{
	delay_notifications ();
	g_atomic_int_inc (&ignore_state_changes);
}

void
Playlist::thaw ()
{
	g_atomic_int_dec_and_test (&ignore_state_changes);
	release_notifications ();
}


void
Playlist::delay_notifications ()
{
	g_atomic_int_inc (&block_notifications);
	freeze_length = _get_extent().second;
}

void
Playlist::release_notifications ()
{
	if (g_atomic_int_dec_and_test (&block_notifications)) {
		flush_notifications ();
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
		pending_length = true;
	} else {
		/* this might not be true, but we have to act
		   as though it could be.
		*/
		pending_length = false;
		LengthChanged (); /* EMIT SIGNAL */
		pending_contents_change = false;
		RegionRemoved (boost::weak_ptr<Region> (r)); /* EMIT SIGNAL */
		ContentsChanged (); /* EMIT SIGNAL */
	}
}

void
Playlist::notify_region_moved (boost::shared_ptr<Region> r)
{
	Evoral::RangeMove<framepos_t> const move (r->last_position (), r->length (), r->position ());

	if (holding_state ()) {

		pending_range_moves.push_back (move);

	} else {

		list< Evoral::RangeMove<framepos_t> > m;
		m.push_back (move);
		RangesMoved (m);
	}

}

void
Playlist::notify_region_added (boost::shared_ptr<Region> r)
{
	/* the length change might not be true, but we have to act
	   as though it could be.
	*/
        
	if (holding_state()) {
		pending_adds.insert (r);
		pending_contents_change = true;
		pending_length = true;
	} else {
		pending_length = false;
		LengthChanged (); /* EMIT SIGNAL */
		pending_contents_change = false;
		RegionAdded (boost::weak_ptr<Region> (r)); /* EMIT SIGNAL */
		ContentsChanged (); /* EMIT SIGNAL */
	}
}

void
Playlist::notify_length_changed ()
{
	if (holding_state ()) {
		pending_length = true;
	} else {
		pending_length = false;
		LengthChanged(); /* EMIT SIGNAL */
		pending_contents_change = false;
		ContentsChanged (); /* EMIT SIGNAL */
	}
}

void
Playlist::flush_notifications ()
{
	set<boost::shared_ptr<Region> > dependent_checks_needed;
	set<boost::shared_ptr<Region> >::iterator s;
	uint32_t regions_changed = false;
	bool check_length = false;
	framecnt_t old_length = 0;

	if (in_flush) {
		return;
	}

	in_flush = true;

	if (!pending_bounds.empty() || !pending_removes.empty() || !pending_adds.empty()) {
		regions_changed = true;
		if (!pending_length) {
			old_length = _get_extent ().second;
			check_length = true;
		}
	}

	/* we have no idea what order the regions ended up in pending
	   bounds (it could be based on selection order, for example).
	   so, to preserve layering in the "most recently moved is higher"
	   model, sort them by existing layer, then timestamp them.
	*/

	// RegionSortByLayer cmp;
	// pending_bounds.sort (cmp);

	for (RegionList::iterator r = pending_bounds.begin(); r != pending_bounds.end(); ++r) {
		if (_session.config.get_layer_model() == MoveAddHigher) {
			timestamp_layer_op (*r);
		}
		dependent_checks_needed.insert (*r);
	}

	for (s = pending_removes.begin(); s != pending_removes.end(); ++s) {
		remove_dependents (*s);
		// cerr << _name << " sends RegionRemoved\n";
		RegionRemoved (boost::weak_ptr<Region> (*s)); /* EMIT SIGNAL */
	}

	for (s = pending_adds.begin(); s != pending_adds.end(); ++s) {
		// cerr << _name << " sends RegionAdded\n";
		RegionAdded (boost::weak_ptr<Region> (*s)); /* EMIT SIGNAL */
		dependent_checks_needed.insert (*s);
	}

	if (check_length) {
		if (old_length != _get_extent().second) {
			pending_length = true;
			// cerr << _name << " length has changed\n";
		}
	}

	if (pending_length || (freeze_length != _get_extent().second)) {
		pending_length = false;
		// cerr << _name << " sends LengthChanged\n";
		LengthChanged(); /* EMIT SIGNAL */
	}

	if (regions_changed || pending_contents_change) {
		if (!in_set_state) {
			relayer ();
		}
		pending_contents_change = false;
		// cerr << _name << " sends 5 contents change @ " << get_microseconds() << endl;
		ContentsChanged (); /* EMIT SIGNAL */
		// cerr << _name << "done contents change @ " << get_microseconds() << endl;
	}

	for (s = dependent_checks_needed.begin(); s != dependent_checks_needed.end(); ++s) {
		check_dependents (*s, false);
	}

	if (!pending_range_moves.empty ()) {
		// cerr << _name << " sends RangesMoved\n";
		RangesMoved (pending_range_moves);
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
	pending_contents_change = false;
	pending_length = false;
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
		partition(pos, (pos + region->length()), true);
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
		boost::shared_ptr<Region> copy = RegionFactory::create (region);
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
	if (region->data_type() != _type){
		return false;
	}

	RegionSortByPosition cmp;

	framecnt_t old_length = 0;

	if (!holding_state()) {
		 old_length = _get_extent().second;
	}

	if (!first_set_state) {
		boost::shared_ptr<Playlist> foo (shared_from_this());
		region->set_playlist (boost::weak_ptr<Playlist>(foo));
	}

	region->set_position (position, this);

	timestamp_layer_op (region);

	regions.insert (upper_bound (regions.begin(), regions.end(), region, cmp), region);
	all_regions.insert (region);

	possibly_splice_unlocked (position, region->length(), region);

	if (!holding_state ()) {
		/* layers get assigned from XML state, and are not reset during undo/redo */
		relayer ();
	}

	/* we need to notify the existence of new region before checking dependents. Ick. */

	notify_region_added (region);

	if (!holding_state ()) {

		check_dependents (region, false);

		if (old_length != _get_extent().second) {
			notify_length_changed ();
		}
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
	framecnt_t old_length = 0;
	int ret = -1;

	if (!holding_state()) {
		old_length = _get_extent().second;
	}

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
                                relayer ();
				remove_dependents (region);

				if (old_length != _get_extent().second) {
					notify_length_changed ();
				}
			}

			notify_region_removed (region);
			ret = 0;
			break;
		}
	}

	/* XXX and thaw ... */

	return ret;
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

void
Playlist::partition_internal (framepos_t start, framepos_t end, bool cutting, RegionList& thawlist)
{
	RegionList new_regions;

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
					plist.add (Properties::layer, regions.size());
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
				plist.add (Properties::layer, regions.size());
				plist.add (Properties::automatic, true);
				plist.add (Properties::right_of_split, true);
				
				region = RegionFactory::create (current, plist);

				add_region_internal (region, end);
				new_regions.push_back (region);

				/* "front" ***** */

				current->suspend_property_changes ();
				thawlist.push_back (current);
				current->cut_end (pos2 - 1, this);

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
					plist.add (Properties::layer, regions.size());
					plist.add (Properties::automatic, true);
					plist.add (Properties::left_of_split, true);

					region = RegionFactory::create (current, plist);

					add_region_internal (region, start);
					new_regions.push_back (region);
				}

				/* front ****** */

				current->suspend_property_changes ();
				thawlist.push_back (current);
				current->cut_end (pos2 - 1, this);

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
					plist.add (Properties::layer, regions.size());
					plist.add (Properties::automatic, true);
					plist.add (Properties::right_of_split, true);

					region = RegionFactory::create (current, plist);

					add_region_internal (region, pos1);
					new_regions.push_back (region);
				}

				/* end */

				current->suspend_property_changes ();
				thawlist.push_back (current);
				current->trim_front (pos3, this);
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

		framecnt_t const old_length = _get_extent().second;

		int itimes = (int) floor (times);
		framepos_t pos = position;
		framecnt_t const shift = other->_get_extent().second;
		layer_t top_layer = regions.size();

		while (itimes--) {
			for (RegionList::iterator i = other->regions.begin(); i != other->regions.end(); ++i) {
				boost::shared_ptr<Region> copy_of_region = RegionFactory::create (*i);

				/* put these new regions on top of all existing ones, but preserve
				   the ordering they had in the original playlist.
				*/

				copy_of_region->set_layer (copy_of_region->layer() + top_layer);
				add_region_internal (copy_of_region, copy_of_region->position() + pos);
			}
			pos += shift;
		}


		/* XXX shall we handle fractional cases at some point? */

		if (old_length != _get_extent().second) {
			notify_length_changed ();
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
	framepos_t pos = position;

	while (itimes--) {
		boost::shared_ptr<Region> copy = RegionFactory::create (region);
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

		if (!ignore_music_glue && (*r)->positional_lock_style() != Region::AudioTime) {
			fixup.push_back (*r);
			continue;
		}

		(*r)->set_position ((*r)->position() + distance, this);
	}

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
		
		plist.add (Properties::start, region->start());
		plist.add (Properties::length, before);
		plist.add (Properties::name, before_name);
		plist.add (Properties::left_of_split, true);
		
		left = RegionFactory::create (region, plist);
	}

	RegionFactory::region_name (after_name, region->name(), false);

	{
		PropertyList plist;
		
		plist.add (Properties::start, region->start() + before);
		plist.add (Properties::length, after);
		plist.add (Properties::name, after_name);
		plist.add (Properties::right_of_split, true);

		right = RegionFactory::create (region, plist);
	}

	add_region_internal (left, region->position());
	add_region_internal (right, region->position() + before);

	uint64_t orig_layer_op = region->last_layer_op();
	for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
		if ((*i)->last_layer_op() > orig_layer_op) {
			(*i)->set_last_layer_op( (*i)->last_layer_op() + 1 );
		}
	}

	left->set_last_layer_op ( orig_layer_op );
	right->set_last_layer_op ( orig_layer_op + 1);

	layer_op_counter++;

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
			} else if (new_pos >= max_frames - (*i)->length()) {
				new_pos = max_frames - (*i)->length();
			}

			(*i)->set_position (new_pos, this);
		}
	}

	_splicing = false;

	notify_length_changed ();
}

void
Playlist::region_bounds_changed (const PropertyChange& what_changed, boost::shared_ptr<Region> region)
{
	if (in_set_state || _splicing || _nudging || _shuffling) {
		return;
	}

	if (what_changed.contains (Properties::position)) {

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
			if (_session.config.get_layer_model() == MoveAddHigher) {
				/* it moved or changed length, so change the timestamp */
				timestamp_layer_op (region);
			}

			notify_length_changed ();
			relayer ();
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

	if (what_changed.contains (Properties::position)) {
		notify_region_moved (region);
	}


	/* don't notify about layer changes, since we are the only object that can initiate
	   them, and we notify in ::relayer()
	*/

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
		pending_length = false;
		LengthChanged ();
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
	framepos_t closest = max_frames;

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
			// r->adjust_to_sync (r->first_frame());
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

	framepos_t closest = max_frames;
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

bool
Playlist::set_property (const PropertyBase& prop)
{
        if (prop == Properties::regions.property_id) {
                const RegionListProperty::ChangeRecord& change (dynamic_cast<const RegionListProperty*>(&prop)->change());
                regions.update (change);
                return (!change.added.empty() && !change.removed.empty());
        }
        return false;
}

void
Playlist::rdiff (vector<StatefulDiffCommand*>& cmds) const
{
	RegionLock rlock (const_cast<Playlist *> (this));

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		if ((*i)->changed ()) {
                        StatefulDiffCommand* sdc = new StatefulDiffCommand (*i);
                        cmds.push_back (sdc);
                }
	}
}

void
Playlist::clear_owned_history ()
{
	RegionLock rlock (this);

	for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
                (*i)->clear_history ();
        }
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

PropertyList*
Playlist::property_factory (const XMLNode& history_node) const
{
        const XMLNodeList& children (history_node.children());
        PropertyList* prop_list = 0;

        for (XMLNodeList::const_iterator i = children.begin(); i != children.end(); ++i) {

                if ((*i)->name() == capitalize (regions.property_name())) {
                        
                        RegionListProperty* rlp = new RegionListProperty (*const_cast<Playlist*> (this));

                        if (rlp->load_history_state (**i)) {
                                if (!prop_list) {
                                        prop_list = new PropertyList();
                                }
                                prop_list->add (rlp);
                        } else {
                                delete rlp;
                        }
                }
        }

        return prop_list;
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

	in_set_state++;

	if (node.name() != "Playlist") {
		in_set_state--;
		return -1;
	}

	freeze ();

	plist = node.properties();

	for (piter = plist.begin(); piter != plist.end(); ++piter) {

		prop = *piter;

		if (prop->name() == X_("name")) {
			_name = prop->value();
                        _set_sort_id ();
		} else if (prop->name() == X_("id")) {
                        _id = prop->value();
		} else if (prop->name() == X_("orig_diskstream_id")) {
			_orig_diskstream_id = prop->value ();
		} else if (prop->name() == X_("frozen")) {
			_frozen = string_is_affirmative (prop->value());
		}
	}

	clear (true);

	nlist = node.children();

	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		child = *niter;

		if (child->name() == "Region") {

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
				continue;
			}
                        
			add_region (region, region->position(), 1.0);

			// So that layer_op ordering doesn't get screwed up
			region->set_last_layer_op( region->layer());
			region->resume_property_changes ();
		}
	}

	/* update dependents, which was not done during add_region_internal
	   due to in_set_state being true
	*/

	for (RegionList::iterator r = regions.begin(); r != regions.end(); ++r) {
		check_dependents (*r, false);
	}

	thaw ();
	notify_contents_changed ();

	in_set_state--;
	first_set_state = false;
	return 0;
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

	_orig_diskstream_id.print (buf, sizeof (buf));
	node->add_property (X_("orig_diskstream_id"), buf);
	node->add_property (X_("frozen"), _frozen ? "yes" : "no");

	if (full_state) {
		RegionLock rlock (this, false);
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

pair<framecnt_t, framecnt_t>
Playlist::get_extent () const
{
	RegionLock rlock (const_cast<Playlist *>(this), false);
	return _get_extent ();
}

pair<framecnt_t, framecnt_t>
Playlist::_get_extent () const
{
	pair<framecnt_t, framecnt_t> ext (max_frames, 0);

	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		pair<framecnt_t, framecnt_t> const e ((*i)->position(), (*i)->position() + (*i)->length());
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

/********************
 * Region Layering
 ********************/

void
Playlist::relayer ()
{
        /* never compute layers when changing state for undo/redo or setting from XML */

        if (in_update || in_set_state) {
                return;
        }

	bool changed = false;

	/* Build up a new list of regions on each layer, stored in a set of lists
	   each of which represent some period of time on some layer.  The idea
	   is to avoid having to search the entire region list to establish whether
	   each region overlaps another */

	/* how many pieces to divide this playlist's time up into */
	int const divisions = 512;

	/* find the start and end positions of the regions on this playlist */
	framepos_t start = INT64_MAX;
	framepos_t end = 0;
	for (RegionList::const_iterator i = regions.begin(); i != regions.end(); ++i) {
		start = min (start, (*i)->position());
		end = max (end, (*i)->position() + (*i)->length());
	}

	/* hence the size of each time division */
	double const division_size = (end - start) / double (divisions);

	vector<vector<RegionList> > layers;
	layers.push_back (vector<RegionList> (divisions));

	/* we want to go through regions from desired lowest to desired highest layer,
	   which depends on the layer model
	*/

	RegionList copy = regions.rlist();

	/* sort according to the model and the layering mode that we're in */

	if (_explicit_relayering) {

		copy.sort (RegionSortByLayerWithPending ());

	} else if (_session.config.get_layer_model() == MoveAddHigher || _session.config.get_layer_model() == AddHigher) {

		copy.sort (RegionSortByLastLayerOp ());

	}


	for (RegionList::iterator i = copy.begin(); i != copy.end(); ++i) {

		/* reset the pending explicit relayer flag for every region, now that we're relayering */
		(*i)->set_pending_explicit_relayer (false);

		/* find the time divisions that this region covers; if there are no regions on the list,
		   division_size will equal 0 and in this case we'll just say that
		   start_division = end_division = 0.
		*/
		int start_division = 0;
		int end_division = 0;

		if (division_size > 0) {
			start_division = floor ( ((*i)->position() - start) / division_size);
			end_division = floor ( ((*i)->position() + (*i)->length() - start) / division_size );
			if (end_division == divisions) {
				end_division--;
			}
		}

		assert (divisions == 0 || end_division < divisions);

		/* find the lowest layer that this region can go on */
		size_t j = layers.size();
		while (j > 0) {
			/* try layer j - 1; it can go on if it overlaps no other region
			   that is already on that layer
			*/

			bool overlap = false;
			for (int k = start_division; k <= end_division; ++k) {
				RegionList::iterator l = layers[j-1][k].begin ();
				while (l != layers[j-1][k].end()) {
					if ((*l)->overlap_equivalent (*i)) {
						overlap = true;
						break;
					}
					l++;
				}

				if (overlap) {
					break;
				}
			}

			if (overlap) {
				/* overlap, so we must use layer j */
				break;
			}

			--j;
		}

		if (j == layers.size()) {
			/* we need a new layer for this region */
			layers.push_back (vector<RegionList> (divisions));
		}

		/* put a reference to this region in each of the divisions that it exists in */
		for (int k = start_division; k <= end_division; ++k) {
			layers[j][k].push_back (*i);
		}
		
		if ((*i)->layer() != j) {
			changed = true;
		}

		(*i)->set_layer (j);
	}

	if (changed) {
		notify_layering_changed ();
	}
}

/* XXX these layer functions are all deprecated */

void
Playlist::raise_region (boost::shared_ptr<Region> region)
{
	uint32_t rsz = regions.size();
	layer_t target = region->layer() + 1U;

	if (target >= rsz) {
		/* its already at the effective top */
		return;
	}

	move_region_to_layer (target, region, 1);
}

void
Playlist::lower_region (boost::shared_ptr<Region> region)
{
	if (region->layer() == 0) {
		/* its already at the bottom */
		return;
	}

	layer_t target = region->layer() - 1U;

	move_region_to_layer (target, region, -1);
}

void
Playlist::raise_region_to_top (boost::shared_ptr<Region> region)
{
	/* does nothing useful if layering mode is later=higher */
	switch (_session.config.get_layer_model()) {
	case LaterHigher:
		return;
	default:
		break;
	}

	layer_t top = regions.size() - 1;

	if (region->layer() >= top) {
		/* already on the top */
		return;
	}

	move_region_to_layer (top, region, 1);
	/* mark the region's last_layer_op as now, so that it remains on top when
	   doing future relayers (until something else takes over)
	 */
	timestamp_layer_op (region);
}

void
Playlist::lower_region_to_bottom (boost::shared_ptr<Region> region)
{
	/* does nothing useful if layering mode is later=higher */
	switch (_session.config.get_layer_model()) {
	case LaterHigher:
		return;
	default:
		break;
	}

	if (region->layer() == 0) {
		/* already on the bottom */
		return;
	}

	move_region_to_layer (0, region, -1);
	/* force region's last layer op to zero so that it stays at the bottom
	   when doing future relayers
	*/
	region->set_last_layer_op (0);
}

int
Playlist::move_region_to_layer (layer_t target_layer, boost::shared_ptr<Region> region, int dir)
{
	RegionList::iterator i;
	typedef pair<boost::shared_ptr<Region>,layer_t> LayerInfo;
	list<LayerInfo> layerinfo;

	{
		RegionLock rlock (const_cast<Playlist *> (this));

		for (i = regions.begin(); i != regions.end(); ++i) {

			if (region == *i) {
				continue;
			}

			layer_t dest;

			if (dir > 0) {

				/* region is moving up, move all regions on intermediate layers
				   down 1
				*/

				if ((*i)->layer() > region->layer() && (*i)->layer() <= target_layer) {
					dest = (*i)->layer() - 1;
				} else {
					/* not affected */
					continue;
				}
			} else {

				/* region is moving down, move all regions on intermediate layers
				   up 1
				*/

				if ((*i)->layer() < region->layer() && (*i)->layer() >= target_layer) {
					dest = (*i)->layer() + 1;
				} else {
					/* not affected */
					continue;
				}
			}

			LayerInfo newpair;

			newpair.first = *i;
			newpair.second = dest;

			layerinfo.push_back (newpair);
		}
	}

	/* now reset the layers without holding the region lock */

	for (list<LayerInfo>::iterator x = layerinfo.begin(); x != layerinfo.end(); ++x) {
		x->first->set_layer (x->second);
	}

	region->set_layer (target_layer);

#if 0
	/* now check all dependents */

	for (list<LayerInfo>::iterator x = layerinfo.begin(); x != layerinfo.end(); ++x) {
		check_dependents (x->first, false);
	}

	check_dependents (region, false);
#endif

	return 0;
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

					if ((*i)->last_frame() > max_frames - distance) {
						new_pos = max_frames - (*i)->length();
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

				(*i)->set_position (new_pos, this);
				moved = true;
			}
		}
	}

	if (moved) {
		_nudging = false;
		notify_length_changed ();
	}

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

boost::shared_ptr<Region>
Playlist::region_by_id (const ID& id)
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
Playlist::timestamp_layer_op (boost::shared_ptr<Region> region)
{
	region->set_last_layer_op (++layer_op_counter);
}


void
Playlist::shuffle (boost::shared_ptr<Region> region, int dir)
{
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

						(*next)->set_position (region->position(), this);
						region->set_position (new_pos, this);

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

						region->set_position ((*prev)->position(), this);
						(*prev)->set_position (new_pos, this);

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

		relayer ();
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
		(*i)->update_position_after_tempo_map_change ();
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

void
Playlist::set_explicit_relayering (bool e)
{
	if (e == false && _explicit_relayering == true) {

		/* We are changing from explicit to implicit relayering; layering may have been changed whilst
		   we were in explicit mode, and we don't want that to be undone next time an implicit relayer
		   occurs.  Hence now we'll set up region last_layer_op values so that an implicit relayer
		   at this point would keep regions on the same layers.

		   From then on in, it's just you and your towel.
		*/

		RegionLock rl (this);
		for (RegionList::iterator i = regions.begin(); i != regions.end(); ++i) {
			(*i)->set_last_layer_op ((*i)->layer ());
		}
	}

	_explicit_relayering = e;
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
