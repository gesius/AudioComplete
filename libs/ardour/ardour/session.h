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

#ifndef __ardour_session_h__
#define __ardour_session_h__

#include <list>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <vector>
#include <stdint.h>

#include <boost/dynamic_bitset.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/utility.hpp>

#include <sndfile.h>

#include <glibmm/thread.h>

#include "pbd/error.h"
#include "pbd/rcu.h"
#include "pbd/statefuldestructible.h"
#include "pbd/undo.h"

#include "midi++/mmc.h"
#include "midi++/types.h"

#include "pbd/destructible.h"
#include "pbd/stateful.h"

#include "ardour/ardour.h"
#include "ardour/click.h"
#include "ardour/chan_count.h"
#include "ardour/rc_configuration.h"
#include "ardour/session_configuration.h"
#include "ardour/session_event.h"
#include "ardour/location.h"
#include "ardour/timecode.h"
#include "ardour/interpolation.h"
#include "ardour/session_playlists.h"

class XMLTree;
class XMLNode;
class AEffect;

namespace MIDI {
	class Port;
}

namespace PBD {
	class Controllable;
}

namespace Evoral {
	class Curve;
}

namespace ARDOUR {

class AudioDiskstream;
class AudioEngine;
class AudioFileSource;
class AudioRegion;
class AudioSource;
class AudioTrack;
class Auditioner;
class AutomationList;
class AuxInput;
class BufferSet;
class Bundle;
class Butler;
class ControlProtocolInfo;
class Diskstream;
class ExportHandler;
class ExportStatus;
class IO;
class IOProcessor;
class ImportStatus;
class MidiDiskstream;
class MidiRegion;
class MidiSource;
class MidiTrack;
class NamedSelection;
class Playlist;
class PluginInsert;
class Port;
class PortInsert;
class Processor;
class Region;
class Return;
class Route;
class RouteGroup;
class SMFSource;
class Send;
class SessionDirectory;
class SessionMetadata;
class Slave;
class Source;
class TempoMap;
class VSTPlugin;

extern void setup_enum_writer ();

class Session : public PBD::StatefulDestructible, public SessionEventManager, public boost::noncopyable
{
  private:
	typedef std::pair<boost::weak_ptr<Route>,bool> RouteBooleanState;
	typedef std::vector<RouteBooleanState> GlobalRouteBooleanState;
	typedef std::pair<boost::weak_ptr<Route>,MeterPoint> RouteMeterState;
	typedef std::vector<RouteMeterState> GlobalRouteMeterState;

  public:
	enum RecordState {
		Disabled = 0,
		Enabled = 1,
		Recording = 2
	};

	/* creating from an XML file */

	Session (AudioEngine&,
			const std::string& fullpath,
			const std::string& snapshot_name,
			std::string mix_template = "");

	/* creating a new Session */

	Session (AudioEngine&,
			std::string fullpath,
			std::string snapshot_name,
			AutoConnectOption input_auto_connect,
			AutoConnectOption output_auto_connect,
			uint32_t control_out_channels,
			uint32_t master_out_channels,
			uint32_t n_physical_in,
			uint32_t n_physical_out,
			nframes_t initial_length);

	virtual ~Session ();

	std::string path() const { return _path; }
	std::string name() const { return _name; }
	std::string snap_name() const { return _current_snapshot_name; }
	std::string raid_path () const;
	bool path_is_within_session (const std::string&);

	void set_snap_name ();

	bool writable() const { return _writable; }
	void set_dirty ();
	void set_clean ();
	bool dirty() const { return _state_of_the_state & Dirty; }
	void set_deletion_in_progress ();
	void clear_deletion_in_progress ();
	bool deletion_in_progress() const { return _state_of_the_state & Deletion; }
	sigc::signal<void> DirtyChanged;

	const SessionDirectory& session_directory () const { return *(_session_dir.get()); }

	static sigc::signal<void> AutoBindingOn;
	static sigc::signal<void> AutoBindingOff;

	static sigc::signal<void,std::string> Dialog;

	std::string sound_dir (bool with_path = true) const;
	std::string peak_dir () const;
	std::string dead_sound_dir () const;
	std::string automation_dir () const;
	std::string analysis_dir() const;

	int ensure_subdirs ();

	Glib::ustring peak_path (Glib::ustring) const;

	static std::string change_source_path_by_name (std::string oldpath, std::string oldname, std::string newname, bool destructive);

	std::string peak_path_from_audio_path (std::string) const;
	std::string new_audio_source_name (const std::string&, uint32_t nchans, uint32_t chan, bool destructive);
	std::string new_midi_source_name (const std::string&);
	std::string new_source_path_from_name (DataType type, const std::string&);
	RouteList new_route_from_template (uint32_t how_many, const std::string& template_path);

	void process (nframes_t nframes);

	BufferSet& get_silent_buffers (ChanCount count = ChanCount::ZERO);
	BufferSet& get_scratch_buffers (ChanCount count = ChanCount::ZERO);
	BufferSet& get_mix_buffers (ChanCount count = ChanCount::ZERO);

	void add_diskstream (boost::shared_ptr<Diskstream>);
	boost::shared_ptr<Diskstream> diskstream_by_id (const PBD::ID& id);
	boost::shared_ptr<Diskstream> diskstream_by_name (std::string name);
	bool have_rec_enabled_diskstream () const;

	bool have_captured() const { return _have_captured; }

	void refill_all_diskstream_buffers ();
	Butler* butler() { return _butler; }
	void butler_transport_work ();

	uint32_t get_next_diskstream_id() const { return n_diskstreams(); }
	uint32_t n_diskstreams() const;

	void refresh_disk_space ();

	typedef std::list<boost::shared_ptr<Diskstream> > DiskstreamList;

	SerializedRCUManager<DiskstreamList>& diskstream_list() { return diskstreams; }

	int load_routes (const XMLNode&, int);
	boost::shared_ptr<RouteList> get_routes() const {
		return routes.reader ();
	}

	boost::shared_ptr<RouteList> get_routes_with_internal_returns() const;

	boost::shared_ptr<RouteList> get_routes_with_regions_at (nframes64_t const) const;

	uint32_t nroutes() const { return routes.reader()->size(); }
	uint32_t ntracks () const;
	uint32_t nbusses () const;

	boost::shared_ptr<BundleList> bundles () {
		return _bundles.reader ();
	}

	struct RoutePublicOrderSorter {
		bool operator() (boost::shared_ptr<Route>, boost::shared_ptr<Route> b);
	};

	void sync_order_keys (std::string const &);

	template<class T> void foreach_route (T *obj, void (T::*func)(Route&));
	template<class T> void foreach_route (T *obj, void (T::*func)(boost::shared_ptr<Route>));
	template<class T, class A> void foreach_route (T *obj, void (T::*func)(Route&, A), A arg);

	boost::shared_ptr<Route> route_by_name (std::string);
	boost::shared_ptr<Route> route_by_id (PBD::ID);
	boost::shared_ptr<Route> route_by_remote_id (uint32_t id);

	bool route_name_unique (std::string) const;
	bool route_name_internal (std::string) const;

	bool get_record_enabled() const {
		return (record_status () >= Enabled);
	}

	RecordState record_status() const {
		return (RecordState) g_atomic_int_get (&_record_status);
	}

	bool actively_recording () {
		return record_status() == Recording;
	}

	bool record_enabling_legal () const;
	void maybe_enable_record ();
	void disable_record (bool rt_context, bool force = false);
	void step_back_from_record ();

	void maybe_write_autosave ();

	/* Proxy signal for region hidden changes */

	sigc::signal<void,boost::shared_ptr<Region> > RegionHiddenChange;

	/* Emitted when all i/o connections are complete */

	sigc::signal<void> IOConnectionsComplete;

	/* Record status signals */

	sigc::signal<void> RecordStateChanged;

	/* Transport mechanism signals */

	sigc::signal<void> TransportStateChange; /* generic */
	sigc::signal<void,nframes64_t> PositionChanged; /* sent after any non-sequential motion */
	sigc::signal<void> DurationChanged;
	sigc::signal<void,nframes64_t> Xrun;
	sigc::signal<void> TransportLooped;

	/** emitted when a locate has occurred */
	sigc::signal<void> Located;

	sigc::signal<void,RouteList&> RouteAdded;
	sigc::signal<void> RouteGroupChanged;

	void request_roll_at_and_return (nframes_t start, nframes_t return_to);
	void request_bounded_roll (nframes_t start, nframes_t end);
	void request_stop (bool abort = false, bool clear_state = false);
	void request_locate (nframes_t frame, bool with_roll = false);

	void request_play_loop (bool yn, bool leave_rolling = false);
	bool get_play_loop () const { return play_loop; }

	nframes_t  last_transport_start() const { return _last_roll_location; }
	void goto_end ()   { request_locate (end_location->start(), false);}
	void goto_start () { request_locate (start_location->start(), false); }
	void set_session_start (nframes_t start) { start_location->set_start(start); }
	void set_session_end (nframes_t end) { end_location->set_start(end); config.set_end_marker_is_free (false); }
	void use_rf_shuttle_speed ();
	void allow_auto_play (bool yn);
	void request_transport_speed (double speed);
	void request_overwrite_buffer (Diskstream*);
	void request_diskstream_speed (Diskstream&, double speed);
	void request_input_change_handling ();

	bool locate_pending() const { return static_cast<bool>(post_transport_work()&PostTransportLocate); }
	bool transport_locked () const;

	int wipe ();

	int remove_region_from_region_list (boost::shared_ptr<Region>);

	nframes_t get_maximum_extent () const;
	nframes_t current_end_frame() const { return end_location->start(); }
	nframes_t current_start_frame() const { return start_location->start(); }
	/// "actual" sample rate of session, set by current audioengine rate, pullup/down etc.
	nframes_t frame_rate() const   { return _current_frame_rate; }
	/// "native" sample rate of session, regardless of current audioengine rate, pullup/down etc
	nframes_t nominal_frame_rate() const   { return _nominal_frame_rate; }
	nframes_t frames_per_hour() const { return _frames_per_hour; }

	double frames_per_timecode_frame() const { return _frames_per_timecode_frame; }
	nframes_t timecode_frames_per_hour() const { return _timecode_frames_per_hour; }

	MIDI::byte get_mtc_timecode_bits() const {
		return mtc_timecode_bits;   /* encoding of SMTPE type for MTC */
	}

	float timecode_frames_per_second() const;
	bool timecode_drop_frames() const;

	/* Locations */

	Locations *locations() { return &_locations; }

	sigc::signal<void,Location*>    auto_loop_location_changed;
	sigc::signal<void,Location*>    auto_punch_location_changed;
	sigc::signal<void>              locations_modified;

	void set_auto_punch_location (Location *);
	void set_auto_loop_location (Location *);
	int location_name(std::string& result, std::string base = std::string(""));

	void reset_input_monitor_state ();

	nframes_t get_block_size()        const { return current_block_size; }
	nframes_t worst_output_latency () const { return _worst_output_latency; }
	nframes_t worst_input_latency ()  const { return _worst_input_latency; }
	nframes_t worst_track_latency ()  const { return _worst_track_latency; }

	int save_state (std::string snapshot_name, bool pending = false);
	int restore_state (std::string snapshot_name);
	int save_template (std::string template_name);
	int save_history (std::string snapshot_name = "");
	int restore_history (std::string snapshot_name);
	void remove_state (std::string snapshot_name);
	void rename_state (std::string old_name, std::string new_name);
	void remove_pending_capture_state ();

	static int rename_template (std::string old_name, std::string new_name);
	static int delete_template (std::string name);

	sigc::signal<void,std::string> StateSaved;
	sigc::signal<void> StateReady;

	std::vector<std::string*>* possible_states() const;
	static std::vector<std::string*>* possible_states (std::string path);

	XMLNode& get_state();
	int      set_state(const XMLNode& node, int version); // not idempotent
	XMLNode& get_template();

	/// The instant xml file is written to the session directory
	void add_instant_xml (XMLNode&, bool write_to_config = true);
	XMLNode* instant_xml (const std::string& str);

	enum StateOfTheState {
		Clean = 0x0,
		Dirty = 0x1,
		CannotSave = 0x2,
		Deletion = 0x4,
		InitialConnecting = 0x8,
		Loading = 0x10,
		InCleanup = 0x20
	};

	StateOfTheState state_of_the_state() const { return _state_of_the_state; }

	void add_route_group (RouteGroup *);
	void remove_route_group (RouteGroup&);

	RouteGroup *route_group_by_name (std::string);

	sigc::signal<void,RouteGroup*> route_group_added;
	sigc::signal<void>             route_group_removed;

	void foreach_route_group (sigc::slot<void,RouteGroup*> sl) {
		for (std::list<RouteGroup *>::iterator i = _route_groups.begin(); i != _route_groups.end(); i++) {
			sl (*i);
		}
	}

	/* fundamental operations. duh. */

	std::list<boost::shared_ptr<AudioTrack> > new_audio_track (
		int input_channels, int output_channels, TrackMode mode = Normal, RouteGroup* route_group = 0, uint32_t how_many = 1
		);

	RouteList new_audio_route (bool aux, int input_channels, int output_channels, RouteGroup* route_group, uint32_t how_many);

	std::list<boost::shared_ptr<MidiTrack> > new_midi_track (
		TrackMode mode = Normal, RouteGroup* route_group = 0, uint32_t how_many = 1
		);

	void   remove_route (boost::shared_ptr<Route>);
	void   resort_routes ();
	void   resort_routes_using (boost::shared_ptr<RouteList>);

	void   set_remote_control_ids();

	AudioEngine & engine() { return _engine; }
	AudioEngine const & engine () const { return _engine; }

	int32_t  max_level;
	int32_t  min_level;

	/* Time */

        nframes64_t transport_frame () const {return _transport_frame; }
	nframes64_t audible_frame () const;
	nframes64_t requested_return_frame() const { return _requested_return_frame; }

	enum PullupFormat {
		pullup_Plus4Plus1,
		pullup_Plus4,
		pullup_Plus4Minus1,
		pullup_Plus1,
		pullup_None,
		pullup_Minus1,
		pullup_Minus4Plus1,
		pullup_Minus4,
		pullup_Minus4Minus1
	};

	void sync_time_vars();

	void bbt_time (nframes_t when, BBT_Time&);
	void timecode_to_sample(Timecode::Time& timecode, nframes_t& sample, bool use_offset, bool use_subframes) const;
	void sample_to_timecode(nframes_t sample, Timecode::Time& timecode, bool use_offset, bool use_subframes) const;
	void timecode_time (Timecode::Time &);
	void timecode_time (nframes_t when, Timecode::Time&);
	void timecode_time_subframes (nframes_t when, Timecode::Time&);

	void timecode_duration (nframes_t, Timecode::Time&) const;
	void timecode_duration_string (char *, nframes_t) const;

	void           set_timecode_offset (nframes_t);
	nframes_t      timecode_offset () const { return _timecode_offset; }
	void           set_timecode_offset_negative (bool);
	bool           timecode_offset_negative () const { return _timecode_offset_negative; }

	nframes_t convert_to_frames_at (nframes_t position, AnyTime const &);

	static sigc::signal<void> StartTimeChanged;
	static sigc::signal<void> EndTimeChanged;
	static sigc::signal<void> TimecodeOffsetChanged;

        std::vector<SyncSource> get_available_sync_options() const;
	void   request_sync_source (Slave*);
        bool   synced_to_jack() const { return config.get_external_sync() && config.get_sync_source() == JACK; }

	double transport_speed() const { return _transport_speed; }
	bool   transport_stopped() const { return _transport_speed == 0.0f; }
	bool   transport_rolling() const { return _transport_speed != 0.0f; }

	void set_silent (bool yn);
	bool silent () { return _silent; }

	int jack_slave_sync (nframes_t);

	TempoMap& tempo_map() { return *_tempo_map; }

	/// signals the current transport position in frames, bbt and timecode time (in that order)
	sigc::signal<void, const nframes_t&, const BBT_Time&, const Timecode::Time&> tick;

	/* region info  */

	void add_regions (std::vector<boost::shared_ptr<Region> >&);

	sigc::signal<void,boost::weak_ptr<Region> > RegionAdded;
	sigc::signal<void,std::vector<boost::weak_ptr<Region> >& > RegionsAdded;
	sigc::signal<void,boost::weak_ptr<Region> > RegionRemoved;

	int region_name (std::string& result, std::string base = std::string(""), bool newlevel = false);
	std::string new_region_name (std::string);
	std::string path_from_region_name (DataType type, std::string name, std::string identifier);

	boost::shared_ptr<Region> find_whole_file_parent (boost::shared_ptr<Region const>);

	boost::shared_ptr<Region>      XMLRegionFactory (const XMLNode&, bool full);
	boost::shared_ptr<AudioRegion> XMLAudioRegionFactory (const XMLNode&, bool full);
	boost::shared_ptr<MidiRegion>  XMLMidiRegionFactory (const XMLNode&, bool full);

	template<class T> void foreach_region (T *obj, void (T::*func)(boost::shared_ptr<Region>));

	/* source management */

	void import_audiofiles (ImportStatus&);
	bool sample_rate_convert (ImportStatus&, std::string infile, std::string& outfile);
	std::string build_tmp_convert_name (std::string file);

	boost::shared_ptr<ExportHandler> get_export_handler ();
	boost::shared_ptr<ExportStatus> get_export_status ();

	int  start_audio_export (nframes_t position, bool realtime);

	sigc::signal<int, nframes_t> ProcessExport;
	sigc::signal<void> ExportReadFinished;
	static sigc::signal<void, std::string, std::string> Exported;

	void add_source (boost::shared_ptr<Source>);
	void remove_source (boost::weak_ptr<Source>);

	int  cleanup_sources (CleanupReport&);
	int  cleanup_trash_sources (CleanupReport&);

	int destroy_region (boost::shared_ptr<Region>);
	int destroy_regions (std::list<boost::shared_ptr<Region> >);

	int remove_last_capture ();

	/** handlers should return -1 for "stop cleanup",
	    0 for "yes, delete this playlist",
	    1 for "no, don't delete this playlist".
	*/
	sigc::signal<int,boost::shared_ptr<Playlist> > AskAboutPlaylistDeletion;

	/** handlers should return 0 for "ignore the rate mismatch",
	    !0 for "do not use this session"
	*/
	static sigc::signal<int,nframes_t, nframes_t> AskAboutSampleRateMismatch;

	/** handlers should return !0 for use pending state, 0 for ignore it.
	*/
	static sigc::signal<int> AskAboutPendingState;

	boost::shared_ptr<AudioFileSource> create_audio_source_for_session (ARDOUR::AudioDiskstream&, uint32_t which_channel, bool destructive);

	boost::shared_ptr<MidiSource> create_midi_source_for_session (ARDOUR::MidiDiskstream&);

	boost::shared_ptr<Source> source_by_id (const PBD::ID&);
	boost::shared_ptr<Source> source_by_path_and_channel (const Glib::ustring&, uint16_t);

	void add_playlist (boost::shared_ptr<Playlist>, bool unused = false);

	/* named selections */

	NamedSelection* named_selection_by_name (std::string name);
	void add_named_selection (NamedSelection *);
	void remove_named_selection (NamedSelection *);

	template<class T> void foreach_named_selection (T& obj, void (T::*func)(NamedSelection&));
	sigc::signal<void> NamedSelectionAdded;
	sigc::signal<void> NamedSelectionRemoved;

	/* Curves and AutomationLists (TODO when they go away) */
	void add_automation_list(AutomationList*);

	/* fade curves */

	float get_default_fade_length () const { return default_fade_msecs; }
	float get_default_fade_steepness () const { return default_fade_steepness; }
	void set_default_fade (float steepness, float msecs);

	/* auditioning */

	boost::shared_ptr<Auditioner> the_auditioner() { return auditioner; }
	void audition_playlist ();
	void audition_region (boost::shared_ptr<Region>);
	void cancel_audition ();
	bool is_auditioning () const;

	sigc::signal<void,bool> AuditionActive;

	/* flattening stuff */

	boost::shared_ptr<Region> write_one_track (AudioTrack&, nframes_t start, nframes_t end,
			bool overwrite, std::vector<boost::shared_ptr<Source> >&, InterThreadInfo& wot,
			bool enable_processing = true);
	int freeze (InterThreadInfo&);

	/* session-wide solo/mute/rec-enable */

	bool soloing() const { return _non_soloed_outs_muted; }
	bool listening() const { return _listen_cnt > 0; }

	void set_all_solo (bool);
	void set_all_mute (bool);
	void set_all_listen (bool);

	sigc::signal<void,bool> SoloActive;
	sigc::signal<void> SoloChanged;

	void record_disenable_all ();
	void record_enable_all ();

	/* control/master out */

	boost::shared_ptr<Route> control_out() const { return _control_out; }
	boost::shared_ptr<Route> master_out() const { return _master_out; }

	void globally_add_internal_sends (boost::shared_ptr<Route> dest, Placement p);
	void globally_set_send_gains_from_track (boost::shared_ptr<Route> dest);
	void globally_set_send_gains_to_zero (boost::shared_ptr<Route> dest);
	void globally_set_send_gains_to_unity (boost::shared_ptr<Route> dest);
	void add_internal_sends (boost::shared_ptr<Route> dest, Placement p, boost::shared_ptr<RouteList> senders);

	static void set_disable_all_loaded_plugins (bool yn) {
		_disable_all_loaded_plugins = yn;
	}
	static bool get_disable_all_loaded_plugins() {
		return _disable_all_loaded_plugins;
	}

	uint32_t next_send_id();
	uint32_t next_return_id();
	uint32_t next_insert_id();
	void mark_send_id (uint32_t);
	void mark_return_id (uint32_t);
	void mark_insert_id (uint32_t);

	/* s/w "RAID" management */

	nframes_t available_capture_duration();

	/* I/O bundles */

	void add_bundle (boost::shared_ptr<Bundle>);
	void remove_bundle (boost::shared_ptr<Bundle>);
	boost::shared_ptr<Bundle> bundle_by_name (std::string) const;

	sigc::signal<void,boost::shared_ptr<Bundle> > BundleAdded;
	sigc::signal<void,boost::shared_ptr<Bundle> > BundleRemoved;

	/* MIDI control */

	void midi_panic(void);
	int set_mtc_port (std::string port_tag);
	int set_mmc_port (std::string port_tag);
	int set_midi_port (std::string port_tag);
	int set_midi_clock_port (std::string port_tag);
	MIDI::Port *mtc_port() const { return _mtc_port; }
	MIDI::Port *mmc_port() const { return _mmc_port; }
	MIDI::Port *midi_port() const { return _midi_port; }
	MIDI::Port *midi_clock_port() const { return _midi_clock_port; }

	sigc::signal<void> MTC_PortChanged;
	sigc::signal<void> MMC_PortChanged;
	sigc::signal<void> MIDI_PortChanged;
	sigc::signal<void> MIDIClock_PortChanged;

	void set_trace_midi_input (bool, MIDI::Port* port = 0);
	void set_trace_midi_output (bool, MIDI::Port* port = 0);

	bool get_trace_midi_input(MIDI::Port *port = 0);
	bool get_trace_midi_output(MIDI::Port *port = 0);

	void set_mmc_receive_device_id (uint32_t id);
	void set_mmc_send_device_id (uint32_t id);

	/* Scrubbing */

	void start_scrub (nframes_t where);
	void stop_scrub ();
	void set_scrub_speed (float);
	nframes_t scrub_buffer_size() const;
	sigc::signal<void> ScrubReady;

	/* History (for editors, mixers, UIs etc.) */

	/** Undo some transactions.
	 * @param n Number of transactions to undo.
	 */
	void undo (uint32_t n) {
		_history.undo (n);
	}

	void redo (uint32_t n) {
		_history.redo (n);
	}

	UndoHistory& history() { return _history; }

	uint32_t undo_depth() const { return _history.undo_depth(); }
	uint32_t redo_depth() const { return _history.redo_depth(); }
	std::string next_undo() const { return _history.next_undo(); }
	std::string next_redo() const { return _history.next_redo(); }

	void begin_reversible_command (const std::string& cmd_name);
	void commit_reversible_command (Command* cmd = 0);

	void add_command (Command *const cmd) {
		assert(!_current_trans.empty ());
		_current_trans.top()->add_command (cmd);
	}

	std::map<PBD::ID, PBD::StatefulThingWithGoingAway*> registry;

	// these commands are implemented in libs/ardour/session_command.cc
	Command* memento_command_factory(XMLNode* n);
	void register_with_memento_command_factory(PBD::ID, PBD::StatefulThingWithGoingAway*);

	Command* global_state_command_factory (const XMLNode& n);

	class GlobalRouteStateCommand : public Command
	{
	public:
		GlobalRouteStateCommand (Session&, void*);
		GlobalRouteStateCommand (Session&, const XMLNode& node);
		int set_state (const XMLNode&, int version);
		XMLNode& get_state ();

	protected:
		GlobalRouteBooleanState before, after;
		Session& sess;
		void* src;
	};

	class GlobalSoloStateCommand : public GlobalRouteStateCommand
	{
	public:
		GlobalSoloStateCommand (Session &, void *src);
		GlobalSoloStateCommand (Session&, const XMLNode&);
		void operator()(); //redo
		void undo();
		XMLNode &get_state();
		void mark();
	};

	class GlobalMuteStateCommand : public GlobalRouteStateCommand
	{
	public:
		GlobalMuteStateCommand(Session &, void *src);
		GlobalMuteStateCommand (Session&, const XMLNode&);
		void operator()(); // redo
		void undo();
		XMLNode &get_state();
		void mark();
	};

	class GlobalRecordEnableStateCommand : public GlobalRouteStateCommand
	{
	public:
		GlobalRecordEnableStateCommand(Session &, void *src);
		GlobalRecordEnableStateCommand (Session&, const XMLNode&);
		void operator()(); // redo
		void undo();
		XMLNode &get_state();
		void mark();
	};

	class GlobalMeteringStateCommand : public Command
	{
	public:
		GlobalMeteringStateCommand(Session &, void *src);
		GlobalMeteringStateCommand (Session&, const XMLNode&);
		void operator()();
		void undo();
		XMLNode &get_state();
		int set_state (const XMLNode&, int version);
		void mark();

	protected:
		Session& sess;
		void* src;
		GlobalRouteMeterState before;
		GlobalRouteMeterState after;
	};

	/* clicking */

	boost::shared_ptr<IO> click_io() { return _click_io; }

	/* disk, buffer loads */

	uint32_t playback_load ();
	uint32_t capture_load ();
	uint32_t playback_load_min ();
	uint32_t capture_load_min ();

	void reset_playback_load_min ();
	void reset_capture_load_min ();

	/* ranges */

	void request_play_range (std::list<AudioRange>*, bool leave_rolling = false);
	bool get_play_range () const { return _play_range; }

	/* buffers for gain and pan */

	gain_t* gain_automation_buffer () const { return _gain_automation_buffer; }
	pan_t** pan_automation_buffer () const  { return _pan_automation_buffer; }

	void ensure_buffer_set (BufferSet& buffers, const ChanCount& howmany);

	/* VST support */

	static long vst_callback (AEffect* effect,
			long opcode,
			long index,
			long value,
			void* ptr,
			float opt);

	static sigc::signal<void> SendFeedback;

	/* Controllables */

	boost::shared_ptr<PBD::Controllable> controllable_by_id (const PBD::ID&);

	void add_controllable (boost::shared_ptr<PBD::Controllable>);
	void remove_controllable (PBD::Controllable*);

	SessionMetadata & metadata () { return *_metadata; }

	SessionConfiguration config;

	bool exporting () const {
		return _exporting;
	}

	/* this is a private enum, but setup_enum_writer() needs it,
	   and i can't find a way to give that function
	   friend access. sigh.
	*/

	enum PostTransportWork {
		PostTransportStop               = 0x1,
		PostTransportDisableRecord      = 0x2,
		PostTransportPosition           = 0x8,
		PostTransportDidRecord          = 0x20,
		PostTransportDuration           = 0x40,
		PostTransportLocate             = 0x80,
		PostTransportRoll               = 0x200,
		PostTransportAbort              = 0x800,
		PostTransportOverWrite          = 0x1000,
		PostTransportSpeed              = 0x2000,
		PostTransportAudition           = 0x4000,
		PostTransportScrub              = 0x8000,
		PostTransportReverse            = 0x10000,
		PostTransportInputChange        = 0x20000,
		PostTransportCurveRealloc       = 0x40000,
		PostTransportClearSubstate      = 0x80000
	};

	enum SlaveState {
		Stopped,
		Waiting,
		Running
	};
	
	SlaveState slave_state() const { return _slave_state; }

	SessionPlaylists playlists;

  protected:
	friend class AudioEngine;
	void set_block_size (nframes_t nframes);
	void set_frame_rate (nframes_t nframes);

  protected:
	friend class Route;
	void schedule_curve_reallocation ();
	void update_latency_compensation (bool, bool);

  private:
	int  create (bool& new_session, const std::string& mix_template, nframes_t initial_length);
	void destroy ();

	nframes_t compute_initial_length ();

	enum SubState {
		PendingDeclickIn   = 0x1,
		PendingDeclickOut  = 0x2,
		StopPendingCapture = 0x4,
		AutoReturning      = 0x10,
		PendingLocate      = 0x20,
		PendingSetLoop     = 0x40
	};

	/* stuff used in process() should be close together to
	   maximise cache hits
	*/

	typedef void (Session::*process_function_type)(nframes_t);

	AudioEngine&            _engine;
	mutable gint             processing_prohibited;
	process_function_type    process_function;
	process_function_type    last_process_function;
	bool                     waiting_for_sync_offset;
	nframes_t               _base_frame_rate;
	nframes_t               _current_frame_rate;  //this includes video pullup offset
	nframes_t               _nominal_frame_rate;  //ignores audioengine setting, "native" SR
	int                      transport_sub_state;
	mutable gint            _record_status;
	volatile nframes64_t    _transport_frame;
	Location*                end_location;
	Location*                start_location;
	Slave*                  _slave;
	bool                    _silent;

    // varispeed playback
	volatile double             _transport_speed;
	double                      _last_transport_speed;
	double                      _target_transport_speed;
	CubicInterpolation          interpolation;

	bool                     auto_play_legal;
	nframes64_t             _last_slave_transport_frame;
	nframes_t                maximum_output_latency;
	volatile nframes64_t    _requested_return_frame;
	BufferSet*              _scratch_buffers;
	BufferSet*              _silent_buffers;
	BufferSet*              _mix_buffers;
	nframes_t                current_block_size;
	nframes_t               _worst_output_latency;
	nframes_t               _worst_input_latency;
	nframes_t               _worst_track_latency;
	bool                    _have_captured;
	float                   _meter_hold;
	float                   _meter_falloff;
	bool                    _non_soloed_outs_muted;
	uint32_t                _listen_cnt;
	bool                    _writable;
	bool                    _was_seamless;

	void set_worst_io_latencies ();
	void set_worst_io_latencies_x (IOChange, void *) {
		set_worst_io_latencies ();
	}

	void update_latency_compensation_proxy (void* ignored);

	void ensure_buffers (ChanCount howmany);

	void process_scrub          (nframes_t);
	void process_without_events (nframes_t);
	void process_with_events    (nframes_t);
	void process_audition       (nframes_t);
	void process_export         (nframes_t);
	int  process_export_fw      (nframes_t);

	void block_processing() { g_atomic_int_set (&processing_prohibited, 1); }
	void unblock_processing() { g_atomic_int_set (&processing_prohibited, 0); }
	bool processing_blocked() const { return g_atomic_int_get (&processing_prohibited); }

	/* slave tracking */

	static const int delta_accumulator_size = 25;
	int delta_accumulator_cnt;
	long delta_accumulator[delta_accumulator_size];
	long average_slave_delta;
	int  average_dir;
	bool have_first_delta_accumulator;

	SlaveState _slave_state;
	nframes_t slave_wait_end;

	void reset_slave_state ();
	bool follow_slave (nframes_t);
	void calculate_moving_average_of_slave_delta(int dir, nframes_t this_delta);
	void track_slave_state(float slave_speed, nframes_t slave_transport_frame, nframes_t this_delta);
	void follow_slave_silently(nframes_t nframes, float slave_speed);

        void switch_to_sync_source (SyncSource); /* !RT context */
        void drop_sync_source ();  /* !RT context */
        void use_sync_source (Slave*); /* RT context */

        bool post_export_sync;
	nframes_t post_export_position;

	bool _exporting;
	bool _exporting_realtime;

	boost::shared_ptr<ExportHandler> export_handler;
	boost::shared_ptr<ExportStatus>  export_status;

	int  pre_export ();
	int  stop_audio_export ();
	void finalize_audio_export ();

	sigc::connection export_freewheel_connection;

	void prepare_diskstreams ();
	void commit_diskstreams (nframes_t, bool& session_requires_butler);
	int  process_routes (nframes_t);
	int  silent_process_routes (nframes_t);

	bool get_rec_monitors_input () {
		if (actively_recording()) {
			return true;
		} else {
			if (config.get_auto_input()) {
				return false;
			} else {
				return true;
			}
		}
	}

	int get_transport_declick_required () {
		if (transport_sub_state & PendingDeclickIn) {
			transport_sub_state &= ~PendingDeclickIn;
			return 1;
		} else if (transport_sub_state & PendingDeclickOut) {
			return -1;
		} else {
			return 0;
		}
	}

	bool maybe_stop (nframes_t limit);
	bool maybe_sync_start (nframes_t&);

	void check_declick_out ();

	MIDI::MachineControl*    mmc;
	MIDI::Port*             _mmc_port;
	MIDI::Port*             _mtc_port;
	MIDI::Port*             _midi_port;
	MIDI::Port*             _midi_clock_port;
	std::string             _path;
	std::string             _name;
	bool                     session_send_mmc;
	bool                     session_send_mtc;
	bool                     session_midi_feedback;
	bool                     play_loop;
	bool                     loop_changing;
	nframes_t                last_loopend;

	boost::scoped_ptr<SessionDirectory> _session_dir;

	void hookup_io ();
	void when_engine_running ();
	void graph_reordered ();

	std::string _current_snapshot_name;

	XMLTree*         state_tree;
	bool             state_was_pending;
	StateOfTheState _state_of_the_state;

	void     auto_save();
	int      load_options (const XMLNode&);
	int      load_state (std::string snapshot_name);

	nframes_t _last_roll_location;
	nframes_t _last_record_location;

	bool              pending_locate_roll;
	nframes_t         pending_locate_frame;
	bool              pending_locate_flush;
	bool              pending_abort;
	bool              pending_auto_loop;

	Butler* _butler;

#if 0 // these should be here, see comments in their other location above
	enum PostTransportWork {
		PostTransportStop               = 0x1,
		PostTransportDisableRecord      = 0x2,
		PostTransportPosition           = 0x8,
		PostTransportDidRecord          = 0x20,
		PostTransportDuration           = 0x40,
		PostTransportLocate             = 0x80,
		PostTransportRoll               = 0x200,
		PostTransportAbort              = 0x800,
		PostTransportOverWrite          = 0x1000,
		PostTransportSpeed              = 0x2000,
		PostTransportAudition           = 0x4000,
		PostTransportScrub              = 0x8000,
		PostTransportReverse            = 0x10000,
		PostTransportInputChange        = 0x20000,
		PostTransportCurveRealloc       = 0x40000,
		PostTransportClearSubstate      = 0x80000
	};
#endif
	static const PostTransportWork ProcessCannotProceedMask =
		PostTransportWork (
				PostTransportInputChange|
				PostTransportSpeed|
				PostTransportReverse|
				PostTransportCurveRealloc|
				PostTransportScrub|
				PostTransportAudition|
				PostTransportLocate|
				PostTransportStop|
				PostTransportClearSubstate);

	gint _post_transport_work; /* accessed only atomic ops */
	PostTransportWork post_transport_work() const        { return (PostTransportWork) g_atomic_int_get (&_post_transport_work); }
	void set_post_transport_work (PostTransportWork ptw) { g_atomic_int_set (&_post_transport_work, (gint) ptw); }
	void add_post_transport_work (PostTransportWork ptw);

	uint32_t    cumulative_rf_motion;
	uint32_t    rf_scale;

	void set_rf_speed (float speed);
	void reset_rf_scale (nframes_t frames_moved);

	Locations        _locations;
	void              locations_changed ();
	void              locations_added (Location*);
	void              handle_locations_changed (Locations::LocationList&);

	sigc::connection auto_punch_start_changed_connection;
	sigc::connection auto_punch_end_changed_connection;
	sigc::connection auto_punch_changed_connection;
	void             auto_punch_start_changed (Location *);
	void             auto_punch_end_changed (Location *);
	void             auto_punch_changed (Location *);

	sigc::connection auto_loop_start_changed_connection;
	sigc::connection auto_loop_end_changed_connection;
	sigc::connection auto_loop_changed_connection;
	void             auto_loop_changed (Location *);

	void first_stage_init (std::string path, std::string snapshot_name);
	int  second_stage_init (bool new_tracks);
	void find_current_end ();
	void remove_empty_sounds ();

	void setup_midi_control ();
	int  midi_read (MIDI::Port *);

	void enable_record ();

	void increment_transport_position (uint32_t val) {
		if (max_frames - val < _transport_frame) {
			_transport_frame = max_frames;
		} else {
			_transport_frame += val;
		}
	}

	void decrement_transport_position (uint32_t val) {
		if (val < _transport_frame) {
			_transport_frame -= val;
		} else {
			_transport_frame = 0;
		}
	}

	void post_transport_motion ();
	static void *session_loader_thread (void *arg);

	void *do_work();

	/* SessionEventManager interface */

	void queue_event (SessionEvent*);
	void process_event (SessionEvent*);
	void set_next_event ();
	void cleanup_event (SessionEvent*,int);

	/* MIDI Machine Control */

	void deliver_mmc (MIDI::MachineControl::Command, nframes_t);

	void spp_start (MIDI::Parser&, nframes_t timestamp);
	void spp_continue (MIDI::Parser&, nframes_t timestamp);
	void spp_stop (MIDI::Parser&, nframes_t timestamp);

	void mmc_deferred_play (MIDI::MachineControl &);
	void mmc_stop (MIDI::MachineControl &);
	void mmc_step (MIDI::MachineControl &, int);
	void mmc_pause (MIDI::MachineControl &);
	void mmc_record_pause (MIDI::MachineControl &);
	void mmc_record_strobe (MIDI::MachineControl &);
	void mmc_record_exit (MIDI::MachineControl &);
	void mmc_track_record_status (MIDI::MachineControl &, uint32_t track, bool enabled);
	void mmc_fast_forward (MIDI::MachineControl &);
	void mmc_rewind (MIDI::MachineControl &);
	void mmc_locate (MIDI::MachineControl &, const MIDI::byte *);
	void mmc_shuttle (MIDI::MachineControl &mmc, float speed, bool forw);
	void mmc_record_enable (MIDI::MachineControl &mmc, size_t track, bool enabled);

	struct timeval last_mmc_step;
	double step_speed;

	typedef sigc::slot<bool> MidiTimeoutCallback;
	typedef std::list<MidiTimeoutCallback> MidiTimeoutList;

	MidiTimeoutList midi_timeouts;
	bool mmc_step_timeout ();

	MIDI::byte mmc_buffer[32];
	MIDI::byte mtc_msg[16];
	MIDI::byte mtc_timecode_bits;   /* encoding of SMTPE type for MTC */
	MIDI::byte midi_msg[16];
	nframes_t  outbound_mtc_timecode_frame;
	Timecode::Time transmitting_timecode_time;
	int next_quarter_frame_to_send;

	double _frames_per_timecode_frame; /* has to be floating point because of drop frame */
	nframes_t _frames_per_hour;
	nframes_t _timecode_frames_per_hour;
	nframes_t _timecode_offset;
	bool _timecode_offset_negative;

	/* cache the most-recently requested time conversions. This helps when we
	 * have multiple clocks showing the same time (e.g. the transport frame) */
	bool           last_timecode_valid;
	nframes_t last_timecode_when;
	Timecode::Time    last_timecode;

	bool _send_timecode_update; ///< Flag to send a full frame (Timecode) MTC message this cycle

	int send_full_time_code(nframes_t nframes);
	int send_midi_time_code_for_cycle(nframes_t nframes);

	nframes_t adjust_apparent_position (nframes_t frames);

	void reset_record_status ();

	int no_roll (nframes_t nframes);
	int fail_roll (nframes_t nframes);

	bool non_realtime_work_pending() const { return static_cast<bool>(post_transport_work()); }
	bool process_can_proceed() const { return !(post_transport_work() & ProcessCannotProceedMask); }

	struct MIDIRequest {
		enum Type {
			PortChange,
			Quit
		};
		Type type;
	};

	Glib::Mutex  midi_lock;
	pthread_t    midi_thread;
	int          midi_request_pipe[2];
	RingBuffer<MIDIRequest*> midi_requests;

	int           start_midi_thread ();
	void          terminate_midi_thread ();
	void          poke_midi_thread ();
	static void *_midi_thread_work (void *arg);
	void          midi_thread_work ();
	void          change_midi_ports ();
	int           use_config_midi_ports ();

	void set_play_loop (bool yn);
	void unset_play_loop ();
	void overwrite_some_buffers (Diskstream*);
	void flush_all_inserts ();
	int  micro_locate (nframes_t distance);
        void locate (nframes64_t, bool with_roll, bool with_flush, bool with_loop=false, bool force=false);
        void start_locate (nframes64_t, bool with_roll, bool with_flush, bool with_loop=false, bool force=false);
	void force_locate (nframes64_t frame, bool with_roll = false);
	void set_diskstream_speed (Diskstream*, double speed);
        void set_transport_speed (double speed, bool abort = false, bool clear_state = false);
	void stop_transport (bool abort = false, bool clear_state = false);
	void start_transport ();
	void realtime_stop (bool abort, bool clear_state);
	void non_realtime_start_scrub ();
	void non_realtime_set_speed ();
	void non_realtime_locate ();
	void non_realtime_stop (bool abort, int entry_request_count, bool& finished);
	void non_realtime_overwrite (int entry_request_count, bool& finished);
	void post_transport ();
	void engine_halted ();
	void xrun_recovery ();

	TempoMap    *_tempo_map;
	void          tempo_map_changed (Change);

	/* edit/mix groups */

	int load_route_groups (const XMLNode&, int);

	std::list<RouteGroup *> _route_groups;

	/* disk-streams */

	SerializedRCUManager<DiskstreamList>  diskstreams;

	int load_diskstreams (const XMLNode&);

	/* routes stuff */

	SerializedRCUManager<RouteList>  routes;

	void add_routes (RouteList&, bool save);
	uint32_t destructive_index;

	boost::shared_ptr<Route> XMLRouteFactory (const XMLNode&, int);

	void route_processors_changed (RouteProcessorChange);

	/* mixer stuff */

	bool solo_update_disabled;

	void route_listen_changed (void *src, boost::weak_ptr<Route>);
	void route_mute_changed (void *src);
	void route_solo_changed (void *src, boost::weak_ptr<Route>);
	void update_route_solo_state (boost::shared_ptr<RouteList> r = boost::shared_ptr<RouteList>());

	void listen_position_changed ();
	void solo_control_mode_changed ();

	/* REGION MANAGEMENT */

	std::map<std::string,uint32_t> region_name_map;
	void update_region_name_map (boost::shared_ptr<Region>);

	mutable Glib::Mutex region_lock;
	typedef std::map<PBD::ID,boost::shared_ptr<Region> > RegionList;
	RegionList regions;

	void add_region (boost::shared_ptr<Region>);
	void region_changed (Change, boost::weak_ptr<Region>);
	void remove_region (boost::weak_ptr<Region>);

	int load_regions (const XMLNode& node);

	void route_group_changed ();

	/* SOURCES */

	mutable Glib::Mutex source_lock;
	typedef std::map<PBD::ID,boost::shared_ptr<Source> > SourceMap;

	SourceMap sources;

  public:
	SourceMap get_sources() { return sources; }

  private:
	int load_sources (const XMLNode& node);
	XMLNode& get_sources_as_xml ();

	boost::shared_ptr<Source> XMLSourceFactory (const XMLNode&);

	/* PLAYLISTS */

	void remove_playlist (boost::weak_ptr<Playlist>);
	void playlist_length_changed ();
	void diskstream_playlist_changed (boost::weak_ptr<Diskstream>);

	/* NAMED SELECTIONS */

	mutable Glib::Mutex named_selection_lock;
	typedef std::set<NamedSelection *> NamedSelectionList;
	NamedSelectionList named_selections;

	int load_named_selections (const XMLNode&);

	NamedSelection *named_selection_factory (std::string name);
	NamedSelection *XMLNamedSelectionFactory (const XMLNode&);

	/* CURVES and AUTOMATION LISTS */
	std::map<PBD::ID, AutomationList*> automation_lists;

	/* DEFAULT FADE CURVES */

	float default_fade_steepness;
	float default_fade_msecs;

	/* AUDITIONING */

	boost::shared_ptr<Auditioner> auditioner;
	void set_audition (boost::shared_ptr<Region>);
	void non_realtime_set_audition ();
	boost::shared_ptr<Region> pending_audition_region;

	/* EXPORT */

	/* FLATTEN */

	int flatten_one_track (AudioTrack&, nframes_t start, nframes_t cnt);

	/* INSERT AND SEND MANAGEMENT */

	boost::dynamic_bitset<uint32_t> send_bitset;
	boost::dynamic_bitset<uint32_t> return_bitset;
	boost::dynamic_bitset<uint32_t> insert_bitset;

	void add_processor (Processor *);
	void remove_processor (Processor *);

	/* S/W RAID */

	struct space_and_path {
		uint32_t blocks; /* 4kB blocks */
		std::string path;

		space_and_path() {
			blocks = 0;
		}
	};

	struct space_and_path_ascending_cmp {
		bool operator() (space_and_path a, space_and_path b) {
			return a.blocks > b.blocks;
		}
	};

	void setup_raid_path (std::string path);

	std::vector<space_and_path> session_dirs;
	std::vector<space_and_path>::iterator last_rr_session_dir;
	uint32_t _total_free_4k_blocks;
	Glib::Mutex space_lock;

	std::string get_best_session_directory_for_new_source ();

	mutable gint _playback_load;
	mutable gint _capture_load;
	mutable gint _playback_load_min;
	mutable gint _capture_load_min;

	/* I/O bundles */

	SerializedRCUManager<BundleList> _bundles;
	XMLNode* _bundle_xml_node;
	int load_bundles (XMLNode const &);

	void reverse_diskstream_buffers ();

	UndoHistory                  _history;
	std::stack<UndoTransaction*> _current_trans;

	GlobalRouteBooleanState get_global_route_boolean (bool (Route::*method)(void) const);
	GlobalRouteMeterState get_global_route_metering ();

	void set_global_route_boolean (GlobalRouteBooleanState s, void (Route::*method)(bool, void*), void *arg);
	void set_global_route_metering (GlobalRouteMeterState s, void *arg);

	void set_global_mute (GlobalRouteBooleanState s, void *src);
	void set_global_solo (GlobalRouteBooleanState s, void *src);
	void set_global_record_enable (GlobalRouteBooleanState s, void *src);

	void jack_timebase_callback (jack_transport_state_t, nframes_t, jack_position_t*, int);
	int  jack_sync_callback (jack_transport_state_t, jack_position_t*);
	void reset_jack_connection (jack_client_t* jack);
	void record_enable_change_all (bool yn);
	void do_record_enable_change_all (RouteList*, bool);

	XMLNode& state(bool);

	/* click track */

	Clicks                 clicks;
	bool                  _clicking;
	boost::shared_ptr<IO> _click_io;
	Sample*                click_data;
	Sample*                click_emphasis_data;
	nframes_t              click_length;
	nframes_t              click_emphasis_length;
	mutable Glib::RWLock   click_lock;

	static const Sample    default_click[];
	static const nframes_t default_click_length;
	static const Sample    default_click_emphasis[];
	static const nframes_t default_click_emphasis_length;

	Click *get_click();
	void   setup_click_sounds (int which);
	void   clear_clicks ();
	void   click (nframes_t start, nframes_t nframes);

	std::vector<Route*> master_outs;

	/* range playback */

	std::list<AudioRange> current_audio_range;
	bool _play_range;
	void set_play_range (std::list<AudioRange>&, bool leave_rolling);
	void unset_play_range ();

	/* main outs */
	uint32_t main_outs;

	boost::shared_ptr<Route> _master_out;
	boost::shared_ptr<Route> _control_out;

	gain_t* _gain_automation_buffer;
	pan_t** _pan_automation_buffer;
	void allocate_pan_automation_buffers (nframes_t nframes, uint32_t howmany, bool force);
	uint32_t _npan_buffers;

	/* VST support */

	long _vst_callback (VSTPlugin*,
			long opcode,
			long index,
			long value,
			void* ptr,
			float opt);

	/* number of hardware ports we're using,
	   based on max (requested,available)
	*/

	uint32_t n_physical_outputs;
	uint32_t n_physical_inputs;

	uint32_t n_physical_audio_outputs;
	uint32_t n_physical_audio_inputs;

	uint32_t n_physical_midi_outputs;
	uint32_t n_physical_midi_inputs;

	int find_all_sources (std::string path, std::set<std::string>& result);
	int find_all_sources_across_snapshots (std::set<std::string>& result, bool exclude_this_snapshot);

	typedef std::set<boost::shared_ptr<PBD::Controllable> > Controllables;
	Glib::Mutex controllables_lock;
	Controllables controllables;

	void reset_native_file_format();
	bool first_file_data_format_reset;
	bool first_file_header_format_reset;

	void config_changed (std::string, bool);

	XMLNode& get_control_protocol_state ();

	void set_history_depth (uint32_t depth);
	void sync_order_keys ();

	static bool _disable_all_loaded_plugins;

	SessionMetadata * _metadata;

	mutable bool have_looped; ///< Used in ::audible_frame(*)

	void update_have_rec_enabled_diskstream ();
	gint _have_rec_enabled_diskstream;
};

} // namespace ARDOUR

#endif /* __ardour_session_h__ */
