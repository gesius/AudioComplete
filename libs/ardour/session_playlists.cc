/*
    Copyright (C) 2009 Paul Davis

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
#include <vector>

#include "pbd/xml++.h"
#include "pbd/compose.h"
#include "ardour/debug.h"
#include "ardour/session_playlists.h"
#include "ardour/playlist.h"
#include "ardour/region.h"
#include "ardour/playlist_factory.h"
#include "ardour/session.h"
#include "i18n.h"

using namespace std;
using namespace PBD;
using namespace ARDOUR;

SessionPlaylists::~SessionPlaylists ()
{
	DEBUG_TRACE (DEBUG::Destruction, "delete playlists\n");
	
	for (List::iterator i = playlists.begin(); i != playlists.end(); ) {
		SessionPlaylists::List::iterator tmp;

		tmp = i;
		++tmp;

		DEBUG_TRACE(DEBUG::Destruction, string_compose ("Dropping for used playlist %1 ; pre-ref = %2\n", (*i)->name(), (*i).use_count()));
		boost::shared_ptr<Playlist> keeper (*i);
		(*i)->drop_references ();

		i = tmp;
	}

	DEBUG_TRACE (DEBUG::Destruction, "delete unused playlists\n");
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ) {
		List::iterator tmp;

		tmp = i;
		++tmp;

		DEBUG_TRACE(DEBUG::Destruction, string_compose ("Dropping for unused playlist %1 ; pre-ref = %2\n", (*i)->name(), (*i).use_count()));
		boost::shared_ptr<Playlist> keeper (*i);
		(*i)->drop_references ();

		i = tmp;
	}

	playlists.clear ();
	unused_playlists.clear ();
}

bool
SessionPlaylists::add (boost::shared_ptr<Playlist> playlist)
{
	Glib::Mutex::Lock lm (lock);

	bool const existing = find (playlists.begin(), playlists.end(), playlist) != playlists.end();

	if (!existing) {
		playlists.insert (playlists.begin(), playlist);
		playlist->InUse.connect_same_thread (*this, boost::bind (&SessionPlaylists::track, this, _1, boost::weak_ptr<Playlist>(playlist)));
	}

	return existing;
}

void
SessionPlaylists::remove (boost::shared_ptr<Playlist> playlist)
{
	Glib::Mutex::Lock lm (lock);

	List::iterator i;

	i = find (playlists.begin(), playlists.end(), playlist);
	if (i != playlists.end()) {
		playlists.erase (i);
	}

	i = find (unused_playlists.begin(), unused_playlists.end(), playlist);
	if (i != unused_playlists.end()) {
		unused_playlists.erase (i);
	}
}
	

void
SessionPlaylists::track (bool inuse, boost::weak_ptr<Playlist> wpl)
{
	boost::shared_ptr<Playlist> pl(wpl.lock());

	if (!pl) {
		return;
	}

	List::iterator x;

	if (pl->hidden()) {
		/* its not supposed to be visible */
		return;
	}

	{
		Glib::Mutex::Lock lm (lock);

		if (!inuse) {

			unused_playlists.insert (pl);

			if ((x = playlists.find (pl)) != playlists.end()) {
				playlists.erase (x);
			}


		} else {

			playlists.insert (pl);

			if ((x = unused_playlists.find (pl)) != unused_playlists.end()) {
				unused_playlists.erase (x);
			}
		}
	}
}

uint32_t
SessionPlaylists::n_playlists () const
{
	Glib::Mutex::Lock lm (lock);
	return playlists.size();
}

boost::shared_ptr<Playlist>
SessionPlaylists::by_name (string name)
{
	Glib::Mutex::Lock lm (lock);

	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if ((*i)->name() == name) {
			return* i;
		}
	}
	
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if ((*i)->name() == name) {
			return* i;
		}
	}

	return boost::shared_ptr<Playlist>();
}

boost::shared_ptr<Playlist>
SessionPlaylists::by_id (const PBD::ID& id)
{
	Glib::Mutex::Lock lm (lock);

	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if ((*i)->id() == id) {
			return* i;
		}
	}
	
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if ((*i)->id() == id) {
			return* i;
		}
	}

	return boost::shared_ptr<Playlist>();
}

void
SessionPlaylists::unassigned (std::list<boost::shared_ptr<Playlist> > & list)
{
	Glib::Mutex::Lock lm (lock);

	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if (!(*i)->get_orig_diskstream_id().to_s().compare ("0")) {
			list.push_back (*i);
		}
	}
	
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if (!(*i)->get_orig_diskstream_id().to_s().compare ("0")) {
			list.push_back (*i);
		}
	}
}

void
SessionPlaylists::get (vector<boost::shared_ptr<Playlist> >& s)
{
	Glib::Mutex::Lock lm (lock);

	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		s.push_back (*i);
	}
	
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		s.push_back (*i);
	}
}

void
SessionPlaylists::destroy_region (boost::shared_ptr<Region> r)
{
	Glib::Mutex::Lock lm (lock);

	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
                (*i)->destroy_region (r);
	}
	
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
                (*i)->destroy_region (r);
	}
}


void
SessionPlaylists::find_equivalent_playlist_regions (boost::shared_ptr<Region> region, vector<boost::shared_ptr<Region> >& result)
{
	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i)
		(*i)->get_region_list_equivalent_regions (region, result);
}

/** Return the number of playlists (not regions) that contain @a src */
uint32_t
SessionPlaylists::source_use_count (boost::shared_ptr<const Source> src) const
{
	uint32_t count = 0;
	for (List::const_iterator p = playlists.begin(); p != playlists.end(); ++p) {
		for (Playlist::RegionList::const_iterator r = (*p)->region_list().begin();
				r != (*p)->region_list().end(); ++r) {
			if ((*r)->uses_source(src)) {
				++count;
				break;
			}
		}
	}
	return count;
}


void
SessionPlaylists::update_after_tempo_map_change ()
{
	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		(*i)->update_after_tempo_map_change ();
	}

	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		(*i)->update_after_tempo_map_change ();
	}
}

void
SessionPlaylists::add_state (XMLNode* node, bool full_state)
{
	XMLNode* child = node->add_child ("Playlists");
	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if (!(*i)->hidden()) {
                        if (full_state) {
                                child->add_child_nocopy ((*i)->get_state());
                        } else {
                                child->add_child_nocopy ((*i)->get_template());
                        }
                }
	}

	child = node->add_child ("UnusedPlaylists");
	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if (!(*i)->hidden()) {
			if (!(*i)->empty()) {
				if (full_state) {
					child->add_child_nocopy ((*i)->get_state());
				} else {
					child->add_child_nocopy ((*i)->get_template());
				}
			}
		}
	}
}

/** @return true for `stop cleanup', otherwise false */
bool
SessionPlaylists::maybe_delete_unused (boost::function<int(boost::shared_ptr<Playlist>)> ask)
{
	vector<boost::shared_ptr<Playlist> > playlists_tbd;

	for (List::iterator x = unused_playlists.begin(); x != unused_playlists.end(); ++x) {

		int status = ask (*x);

		switch (status) {
		case -1:
			return true;

		case 0:
			playlists_tbd.push_back (*x);
			break;

		default:
			/* leave it alone */
			break;
		}
	}

	/* now delete any that were marked for deletion */

	for (vector<boost::shared_ptr<Playlist> >::iterator x = playlists_tbd.begin(); x != playlists_tbd.end(); ++x) {
		boost::shared_ptr<Playlist> keeper (*x);
		(*x)->drop_references ();
	}

	playlists_tbd.clear ();

	return false;
}

int
SessionPlaylists::load (Session& session, const XMLNode& node)
{
	XMLNodeList nlist;
	XMLNodeConstIterator niter;
	boost::shared_ptr<Playlist> playlist;

	nlist = node.children();

	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		if ((playlist = XMLPlaylistFactory (session, **niter)) == 0) {
			error << _("Session: cannot create Playlist from XML description.") << endmsg;
		}
	}

	return 0;
}

int
SessionPlaylists::load_unused (Session& session, const XMLNode& node)
{
	XMLNodeList nlist;
	XMLNodeConstIterator niter;
	boost::shared_ptr<Playlist> playlist;

	nlist = node.children();

	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		if ((playlist = XMLPlaylistFactory (session, **niter)) == 0) {
			error << _("Session: cannot create Playlist from XML description.") << endmsg;
			continue;
		}

		// now manually untrack it

		track (false, boost::weak_ptr<Playlist> (playlist));
	}

	return 0;
}

boost::shared_ptr<Playlist>
SessionPlaylists::XMLPlaylistFactory (Session& session, const XMLNode& node)
{
	try {
		return PlaylistFactory::create (session, node);
	}

	catch (failed_constructor& err) {
		return boost::shared_ptr<Playlist>();
	}
}

boost::shared_ptr<Crossfade>
SessionPlaylists::find_crossfade (const PBD::ID& id)
{
	Glib::Mutex::Lock lm (lock);

	boost::shared_ptr<Crossfade> c;
	
	for (List::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		c = (*i)->find_crossfade (id);
		if (c) {
			return c;
		}
	}

	for (List::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		c = (*i)->find_crossfade (id);
		if (c) {
			return c;
		}
	}

	return boost::shared_ptr<Crossfade> ();
}
