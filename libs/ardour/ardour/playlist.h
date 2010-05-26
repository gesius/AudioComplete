/*
    Copyright (C) 2000 Paul Davis

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

#ifndef __ardour_playlist_h__
#define __ardour_playlist_h__

#include <string>
#include <set>
#include <map>
#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/utility.hpp>

#include <sys/stat.h>

#include <glib.h>


#include "pbd/undo.h"
#include "pbd/stateful.h"
#include "pbd/stateful_owner.h"
#include "pbd/statefuldestructible.h"
#include "pbd/sequence_property.h"

#include "evoral/types.hpp"

#include "ardour/ardour.h"
#include "ardour/session_object.h"
#include "ardour/crossfade_compare.h"
#include "ardour/location.h"
#include "ardour/data_type.h"

namespace ARDOUR  {

class Session;
class Region;
class Playlist;

namespace Properties {
        /* fake the type, since regions are handled by SequenceProperty which doesn't
           care about such things.
        */
        extern PBD::PropertyDescriptor<bool> regions;
}

class RegionListProperty : public PBD::SequenceProperty<std::list<boost::shared_ptr<Region > > >
{
  public:
        RegionListProperty (Playlist&);

        boost::shared_ptr<Region> lookup_id (const PBD::ID& id);
        void diff (PBD::PropertyList& undo, PBD::PropertyList& redo) const;

  private:
        friend class Playlist;
        std::list<boost::shared_ptr<Region> > rlist() { return _val; }

        /* we live and die with our playlist, no lifetime management needed */
        Playlist& _playlist;

        /* create a copy of this RegionListProperty that only
           has what is needed for use in a history list command. This
           means that it won't contain the actual region list but
           will have the added/removed list.
        */
        RegionListProperty* copy_for_history () const;
};

class Playlist : public SessionObject
               , public PBD::StatefulOwner
	       , public boost::enable_shared_from_this<Playlist> {
  public:
	typedef std::list<boost::shared_ptr<Region> >    RegionList;
        static void make_property_quarks ();

	Playlist (Session&, const XMLNode&, DataType type, bool hidden = false);
	Playlist (Session&, std::string name, DataType type, bool hidden = false);
	Playlist (boost::shared_ptr<const Playlist>, std::string name, bool hidden = false);
	Playlist (boost::shared_ptr<const Playlist>, framepos_t start, framecnt_t cnt, std::string name, bool hidden = false);

	virtual ~Playlist ();

        bool set_property (const PBD::PropertyBase&);
        void update (const RegionListProperty::ChangeRecord&);
        void clear_owned_history ();
        void rdiff (std::vector<PBD::StatefulDiffCommand*>&) const;

        PBD::PropertyList* property_factory (const XMLNode&) const;

	boost::shared_ptr<Region> region_by_id (const PBD::ID&);

	void set_region_ownership ();

	virtual void clear (bool with_signals=true);
	virtual void dump () const;

	void use();
	void release();
	bool used () const { return _refcnt != 0; }

	bool set_name (const std::string& str);
        int sort_id() { return _sort_id; }

	const DataType& data_type() const { return _type; }

	bool frozen() const { return _frozen; }
	void set_frozen (bool yn);

	bool hidden() const { return _hidden; }
	bool empty() const;
	uint32_t n_regions() const;
	std::pair<framecnt_t, framecnt_t> get_extent () const;
	layer_t top_layer() const;

	EditMode get_edit_mode() const { return _edit_mode; }
	void set_edit_mode (EditMode);

	/* Editing operations */

	void add_region (boost::shared_ptr<Region>, framepos_t position, float times = 1, bool auto_partition = false);
	void remove_region (boost::shared_ptr<Region>);
	void remove_region_by_source (boost::shared_ptr<Source>);
	void get_equivalent_regions (boost::shared_ptr<Region>, std::vector<boost::shared_ptr<Region> >&);
	void get_region_list_equivalent_regions (boost::shared_ptr<Region>, std::vector<boost::shared_ptr<Region> >&);
	void replace_region (boost::shared_ptr<Region> old, boost::shared_ptr<Region> newr, framepos_t pos);
	void split_region (boost::shared_ptr<Region>, framepos_t position);
	void split (framepos_t at);
	void shift (framepos_t at, frameoffset_t distance, bool move_intersected, bool ignore_music_glue);
	void partition (framepos_t start, framepos_t end, bool cut = false);
	void duplicate (boost::shared_ptr<Region>, framepos_t position, float times);
	void nudge_after (framepos_t start, framecnt_t distance, bool forwards);
	void shuffle (boost::shared_ptr<Region>, int dir);
	void update_after_tempo_map_change ();

	boost::shared_ptr<Playlist> cut  (std::list<AudioRange>&, bool result_is_hidden = true);
	boost::shared_ptr<Playlist> copy (std::list<AudioRange>&, bool result_is_hidden = true);
	int                         paste (boost::shared_ptr<Playlist>, framepos_t position, float times);

	const RegionListProperty& region_list () const { return regions; }

	RegionList*                regions_at (framepos_t frame);
	RegionList*                regions_touched (framepos_t start, framepos_t end);
	RegionList*                regions_to_read (framepos_t start, framepos_t end);
	boost::shared_ptr<Region>  find_region (const PBD::ID&) const;
	boost::shared_ptr<Region>  top_region_at (framepos_t frame);
	boost::shared_ptr<Region>  top_unmuted_region_at (framepos_t frame);
	boost::shared_ptr<Region>  find_next_region (framepos_t frame, RegionPoint point, int dir);
	framepos_t                 find_next_region_boundary (framepos_t frame, int dir);
	bool                       region_is_shuffle_constrained (boost::shared_ptr<Region>);
	bool                       has_region_at (framepos_t const) const;


	framepos_t find_next_transient (framepos_t position, int dir);

	void foreach_region (boost::function<void (boost::shared_ptr<Region>)>);

	XMLNode& get_state ();
	int set_state (const XMLNode&, int version);
	XMLNode& get_template ();

	PBD::Signal1<void,bool> InUse;
	PBD::Signal0<void>      ContentsChanged;
	PBD::Signal1<void,boost::weak_ptr<Region> > RegionAdded;
	PBD::Signal1<void,boost::weak_ptr<Region> > RegionRemoved;
	PBD::Signal0<void>      NameChanged;
	PBD::Signal0<void>      LengthChanged;
	PBD::Signal0<void>      LayeringChanged;
	PBD::Signal1<void,std::list< Evoral::RangeMove<framepos_t> > const &> RangesMoved;

	static std::string bump_name (std::string old_name, Session&);

	void freeze ();
	void thaw ();

	void raise_region (boost::shared_ptr<Region>);
	void lower_region (boost::shared_ptr<Region>);
	void raise_region_to_top (boost::shared_ptr<Region>);
	void lower_region_to_bottom (boost::shared_ptr<Region>);

	uint32_t read_data_count() const { return _read_data_count; }

	/* XXX: use of diskstream here is a little unfortunate */
	const PBD::ID& get_orig_diskstream_id () const { return _orig_diskstream_id; }
	void set_orig_diskstream_id (const PBD::ID& did) { _orig_diskstream_id = did; }

	/* destructive editing */

	virtual bool destroy_region (boost::shared_ptr<Region>) = 0;

	/* special case function used by UI selection objects, which have playlists that actually own the regions
	   within them.
	*/

	void drop_regions ();

	bool explicit_relayering () const {
		return _explicit_relayering;
	}

	void set_explicit_relayering (bool e);

  protected:
	friend class Session;

  protected:
	struct RegionLock {
		RegionLock (Playlist *pl, bool do_block_notify = true) : playlist (pl), block_notify (do_block_notify) {
			playlist->region_lock.lock();
			if (block_notify) {
				playlist->delay_notifications();
			}
		}
		~RegionLock() {
			playlist->region_lock.unlock();
			if (block_notify) {
				playlist->release_notifications ();
			}
		}
		Playlist *playlist;
		bool block_notify;
	};

	friend class RegionLock;

        RegionListProperty   regions;  /* the current list of regions in the playlist */
	std::set<boost::shared_ptr<Region> > all_regions; /* all regions ever added to this playlist */
	PBD::ScopedConnectionList region_state_changed_connections;
	DataType        _type;
        int             _sort_id;
	mutable gint    block_notifications;
	mutable gint    ignore_state_changes;
	mutable Glib::RecMutex region_lock;
	std::set<boost::shared_ptr<Region> > pending_adds;
	std::set<boost::shared_ptr<Region> > pending_removes;
	RegionList       pending_bounds;
	bool             pending_contents_change;
	bool             pending_layering;
	bool             pending_length;
	std::list< Evoral::RangeMove<framepos_t> > pending_range_moves;
	bool             save_on_thaw;
	std::string      last_save_reason;
	uint32_t         in_set_state;
	bool             in_update;
	bool             first_set_state;
	bool            _hidden;
	bool            _splicing;
	bool            _shuffling;
	bool            _nudging;
	uint32_t        _refcnt;
	EditMode        _edit_mode;
	bool             in_flush;
	bool             in_partition;
	bool            _frozen;
	uint32_t         subcnt;
	uint32_t        _read_data_count;
	PBD::ID         _orig_diskstream_id;
	uint64_t         layer_op_counter;
	framecnt_t       freeze_length;
	bool             auto_partition;

	/** true if relayering should be done using region's current layers and their `pending explicit relayer'
	 *  flags; otherwise false if relayering should be done using the layer-model (most recently moved etc.)
	 *  Explicit relayering is used by tracks in stacked regionview mode.
	 */
	bool            _explicit_relayering;

	void init (bool hide);

	bool holding_state () const {
		return g_atomic_int_get (&block_notifications) != 0 ||
			g_atomic_int_get (&ignore_state_changes) != 0;
	}

	void delay_notifications ();
	void release_notifications ();
	virtual void flush_notifications ();
	void clear_pending ();

        void _set_sort_id ();

	void notify_region_removed (boost::shared_ptr<Region>);
	void notify_region_added (boost::shared_ptr<Region>);
	void notify_length_changed ();
	void notify_layering_changed ();
	void notify_contents_changed ();
	void notify_state_changed (const PBD::PropertyChange&);
	void notify_region_moved (boost::shared_ptr<Region>);

	void mark_session_dirty();

	void region_changed_proxy (const PBD::PropertyChange&, boost::weak_ptr<Region>);
	virtual bool region_changed (const PBD::PropertyChange&, boost::shared_ptr<Region>);

	void region_bounds_changed (const PBD::PropertyChange&, boost::shared_ptr<Region>);
	void region_deleted (boost::shared_ptr<Region>);

	void sort_regions ();

	void possibly_splice (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude = boost::shared_ptr<Region>());
	void possibly_splice_unlocked(framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude = boost::shared_ptr<Region>());

	void core_splice (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude);
	void splice_locked (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude);
	void splice_unlocked (framepos_t at, framecnt_t distance, boost::shared_ptr<Region> exclude);

	virtual void finalize_split_region (boost::shared_ptr<Region> /*original*/, boost::shared_ptr<Region> /*left*/, boost::shared_ptr<Region> /*right*/) {}

	virtual void check_dependents (boost::shared_ptr<Region> /*region*/, bool /*norefresh*/) {}
	virtual void refresh_dependents (boost::shared_ptr<Region> /*region*/) {}
	virtual void remove_dependents (boost::shared_ptr<Region> /*region*/) {}

	virtual XMLNode& state (bool);

	bool add_region_internal (boost::shared_ptr<Region>, framepos_t position);

	int remove_region_internal (boost::shared_ptr<Region>);
	RegionList *find_regions_at (framepos_t frame);
	void copy_regions (RegionList&) const;
	void partition_internal (framepos_t start, framepos_t end, bool cutting, RegionList& thawlist);

	std::pair<framecnt_t, framecnt_t> _get_extent() const;

	boost::shared_ptr<Playlist> cut_copy (boost::shared_ptr<Playlist> (Playlist::*pmf)(framepos_t, framecnt_t, bool),
					      std::list<AudioRange>& ranges, bool result_is_hidden);
	boost::shared_ptr<Playlist> cut (framepos_t start, framecnt_t cnt, bool result_is_hidden);
	boost::shared_ptr<Playlist> copy (framepos_t start, framecnt_t cnt, bool result_is_hidden);

	int move_region_to_layer (layer_t, boost::shared_ptr<Region> r, int dir);
	void relayer ();

	void begin_undo ();
	void end_undo ();
	void unset_freeze_parent (Playlist*);
	void unset_freeze_child (Playlist*);

	void timestamp_layer_op (boost::shared_ptr<Region>);

	void _split_region (boost::shared_ptr<Region>, framepos_t position);
};

} /* namespace ARDOUR */

#endif	/* __ardour_playlist_h__ */


