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

#include <glibmm/thread.h>

#include "pbd/error.h"
#include "pbd/rcu.h"
#include "pbd/statefuldestructible.h"
#include "pbd/signals.h"
#include "pbd/undo.h"

#include "midi++/types.h"

#include "ardour/ardour.h"
#include "ardour/click.h"
#include "ardour/chan_count.h"
#include "ardour/rc_configuration.h"
#include "ardour/session_configuration.h"
#include "ardour/session_event.h"
#include "ardour/location.h"
#include "ardour/timecode.h"
#include "ardour/interpolation.h"
#include "ardour/speakers.h"

#ifdef HAVE_JACK_SESSION
#include <jack/session.h>
#endif

class XMLTree;
class XMLNode;
class AEffect;

namespace MIDI {
	class Port;
	class MachineControl;
	class Parser;
}

namespace PBD {
	class Controllable;
	class ControllableDescriptor;
}

namespace Evoral {
	class Curve;
}

namespace ARDOUR {

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
class Diskstream;
class ExportHandler;
class ExportStatus;
class IO;
class IOProcessor;
class ImportStatus;
class MidiRegion;
class MidiSource;
class MidiTrack;
class MidiControlUI;
class NamedSelection;
class Playlist;
class PluginInsert;
class Port;
class PortInsert;
class ProcessThread;
class Processor;
class Region;
class Return;
class Route;
class RouteGroup;
class SMFSource;
class Send;
class SessionDirectory;
class SessionMetadata;
class SessionPlaylists;
class Slave;
class Source;
class Speakers;
class TempoMap;
class VSTPlugin;
class Graph;
class Track;

extern void setup_enum_writer ();

class Session : public PBD::StatefulDestructible, public PBD::ScopedConnectionList, public SessionEventManager
{
  public:
	enum RecordState {
		Disabled = 0,
		Enabled = 1,
		Recording = 2
	};

	/* a new session might have non-empty mix_template, an existing session should always have an empty one.
	   the bus profile can be null if no master out bus is required.
	*/

	Session (AudioEngine&,
	         const std::string& fullpath,
	         const std::string& snapshot_name,
	         BusProfile* bus_profile = 0,
	         std::string mix_template = "");

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
	PBD::Signal0<void> DirtyChanged;

	const SessionDirectory& session_directory () const { return *(_session_dir.get()); }

	static PBD::Signal0<void> AutoBindingOn;
	static PBD::Signal0<void> AutoBindingOff;

	static PBD::Signal1<void,std::string> Dialog;

	std::string sound_dir (bool with_path = true) const;
	std::string peak_dir () const;
	std::string dead_sound_dir () const;
	std::string automation_dir () const;
	std::string analysis_dir() const;
	std::string plugins_dir() const;
	
	int ensure_subdirs ();

	std::string peak_path (std::string) const;

	std::string change_source_path_by_name (std::string oldpath, std::string oldname, std::string newname, bool destructive);

	std::string peak_path_from_audio_path (std::string) const;
	std::string new_audio_source_name (const std::string&, uint32_t nchans, uint32_t chan, bool destructive);
	std::string new_midi_source_name (const std::string&);
	std::string new_source_path_from_name (DataType type, const std::string&, bool as_stub = false);
	RouteList new_route_from_template (uint32_t how_many, const std::string& template_path);

	void process (pframes_t nframes);

	BufferSet& get_silent_buffers (ChanCount count = ChanCount::ZERO);
	BufferSet& get_scratch_buffers (ChanCount count = ChanCount::ZERO);
	BufferSet& get_mix_buffers (ChanCount count = ChanCount::ZERO);

	bool have_rec_enabled_track () const;

	bool have_captured() const { return _have_captured; }

	void refill_all_track_buffers ();
	Butler* butler() { return _butler; }
	void butler_transport_work ();

	void refresh_disk_space ();

	int load_diskstreams_2X (XMLNode const &, int);
	
	int load_routes (const XMLNode&, int);
	boost::shared_ptr<RouteList> get_routes() const {
		return routes.reader ();
	}

	boost::shared_ptr<RouteList> get_routes_with_internal_returns() const;

	boost::shared_ptr<RouteList> get_routes_with_regions_at (framepos_t const) const;

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

	bool io_name_is_legal (const std::string&);
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

	/* Emitted when all i/o connections are complete */

	PBD::Signal0<void> IOConnectionsComplete;

	/* Record status signals */

	PBD::Signal0<void> RecordStateChanged;

	/* Transport mechanism signals */

	PBD::Signal0<void> TransportStateChange; /* generic */
	PBD::Signal1<void,framepos_t> PositionChanged; /* sent after any non-sequential motion */
	PBD::Signal1<void,framepos_t> Xrun;
	PBD::Signal0<void> TransportLooped;

	/** emitted when a locate has occurred */
	PBD::Signal0<void> Located;

	PBD::Signal1<void,RouteList&> RouteAdded;
	/** Emitted when anything about any of our route groups changes */
	PBD::Signal0<void> RouteGroupChanged;

	/* Step Editing status changed */
	PBD::Signal1<void,bool> StepEditStatusChange;

	void queue_event (SessionEvent*);

	void request_roll_at_and_return (framepos_t start, framepos_t return_to);
	void request_bounded_roll (framepos_t start, framepos_t end);
	void request_stop (bool abort = false, bool clear_state = false);
	void request_locate (framepos_t frame, bool with_roll = false);

	void request_play_loop (bool yn, bool leave_rolling = false);
	bool get_play_loop () const { return play_loop; }

	framepos_t last_transport_start () const { return _last_roll_location; }
	void goto_end ();
	void goto_start ();
	void use_rf_shuttle_speed ();
	void allow_auto_play (bool yn);
	void request_transport_speed (double speed);
	void request_overwrite_buffer (Track *);
	void adjust_playback_buffering();
	void adjust_capture_buffering();
	void request_track_speed (Track *, double speed);
	void request_input_change_handling ();

	bool locate_pending() const { return static_cast<bool>(post_transport_work()&PostTransportLocate); }
	bool transport_locked () const;

	int wipe ();

	std::pair<framepos_t, framepos_t> get_extent () const;
	framepos_t current_end_frame () const;
	framepos_t current_start_frame () const;
	/** "actual" sample rate of session, set by current audioengine rate, pullup/down etc. */
	framecnt_t frame_rate () const { return _current_frame_rate; }
	/** "native" sample rate of session, regardless of current audioengine rate, pullup/down etc */
	framecnt_t nominal_frame_rate () const { return _nominal_frame_rate; }
	framecnt_t frames_per_hour () const { return _frames_per_hour; }

	double frames_per_timecode_frame() const { return _frames_per_timecode_frame; }
	framecnt_t timecode_frames_per_hour() const { return _timecode_frames_per_hour; }

	MIDI::byte get_mtc_timecode_bits() const {
		return mtc_timecode_bits;   /* encoding of SMTPE type for MTC */
	}

	float timecode_frames_per_second() const;
	bool timecode_drop_frames() const;

	/* Locations */

	Locations *locations() { return _locations; }

	PBD::Signal1<void,Location*>    auto_loop_location_changed;
	PBD::Signal1<void,Location*>    auto_punch_location_changed;
	PBD::Signal0<void>              locations_modified;

	void set_auto_punch_location (Location *);
	void set_auto_loop_location (Location *);
	int location_name(std::string& result, std::string base = std::string(""));

	void reset_input_monitor_state ();

	pframes_t get_block_size()        const { return current_block_size; }
	framecnt_t worst_output_latency () const { return _worst_output_latency; }
	framecnt_t worst_input_latency ()  const { return _worst_input_latency; }
	framecnt_t worst_track_latency ()  const { return _worst_track_latency; }

#ifdef HAVE_JACK_SESSION 
	void jack_session_event (jack_session_event_t* event);
#endif
	int save_state (std::string snapshot_name, bool pending = false, bool switch_to_snapshot = false);
	int restore_state (std::string snapshot_name);
	int save_template (std::string template_name);
	int save_history (std::string snapshot_name = "");
	int restore_history (std::string snapshot_name);
	void remove_state (std::string snapshot_name);
	void rename_state (std::string old_name, std::string new_name);
	void remove_pending_capture_state ();

	static int rename_template (std::string old_name, std::string new_name);
	static int delete_template (std::string name);

	PBD::Signal1<void,std::string> StateSaved;
	PBD::Signal0<void> StateReady;

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

	RouteGroup* route_group_by_name (std::string);
	RouteGroup& all_route_group() const;

	PBD::Signal1<void,RouteGroup*> route_group_added;
	PBD::Signal0<void>             route_group_removed;

	void foreach_route_group (boost::function<void(RouteGroup*)> f) {
		for (std::list<RouteGroup *>::iterator i = _route_groups.begin(); i != _route_groups.end(); ++i) {
			f (*i);
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

	/* Time */

	framepos_t transport_frame () const {return _transport_frame; }
	framepos_t audible_frame () const;
	framepos_t requested_return_frame() const { return _requested_return_frame; }

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

	void bbt_time (framepos_t when, BBT_Time&);
	void timecode_to_sample(Timecode::Time& timecode, framepos_t& sample, bool use_offset, bool use_subframes) const;
	void sample_to_timecode(framepos_t sample, Timecode::Time& timecode, bool use_offset, bool use_subframes) const;
	void timecode_time (Timecode::Time &);
	void timecode_time (framepos_t when, Timecode::Time&);
	void timecode_time_subframes (framepos_t when, Timecode::Time&);

	void timecode_duration (framecnt_t, Timecode::Time&) const;
	void timecode_duration_string (char *, framecnt_t) const;

	framecnt_t convert_to_frames_at (framepos_t position, AnyTime const &);

	static PBD::Signal1<void, framepos_t> StartTimeChanged;
	static PBD::Signal1<void, framepos_t> EndTimeChanged;

	std::vector<SyncSource> get_available_sync_options() const;
	void   request_sync_source (Slave*);
	bool   synced_to_jack() const { return config.get_external_sync() && config.get_sync_source() == JACK; }

	double transport_speed() const { return _transport_speed; }
	bool   transport_stopped() const { return _transport_speed == 0.0f; }
	bool   transport_rolling() const { return _transport_speed != 0.0f; }

	void set_silent (bool yn);
	bool silent () { return _silent; }

	TempoMap& tempo_map() { return *_tempo_map; }

	/// signals the current transport position in frames, bbt and timecode time (in that order)
	PBD::Signal3<void, const framepos_t &, const BBT_Time&, const Timecode::Time&> tick;

	/* region info  */

	boost::shared_ptr<Region> find_whole_file_parent (boost::shared_ptr<Region const>) const;

	std::string path_from_region_name (DataType type, std::string name, std::string identifier);

	boost::shared_ptr<Region>      XMLRegionFactory (const XMLNode&, bool full);
	boost::shared_ptr<AudioRegion> XMLAudioRegionFactory (const XMLNode&, bool full);
	boost::shared_ptr<MidiRegion>  XMLMidiRegionFactory (const XMLNode&, bool full);

	/* source management */

	void import_audiofiles (ImportStatus&);
	bool sample_rate_convert (ImportStatus&, std::string infile, std::string& outfile);
	std::string build_tmp_convert_name (std::string file);

	boost::shared_ptr<ExportHandler> get_export_handler ();
	boost::shared_ptr<ExportStatus> get_export_status ();

	int start_audio_export (framepos_t position, bool realtime);

	PBD::Signal1<int, framecnt_t> ProcessExport;
	static PBD::Signal2<void,std::string, std::string> Exported;

	void add_source (boost::shared_ptr<Source>);
	void remove_source (boost::weak_ptr<Source>);

	int  cleanup_sources (CleanupReport&);
	int  cleanup_trash_sources (CleanupReport&);

	int destroy_sources (std::list<boost::shared_ptr<Source> >);

	int remove_last_capture ();

	/** handlers should return -1 for "stop cleanup",
	    0 for "yes, delete this playlist",
	    1 for "no, don't delete this playlist".
	*/
	static PBD::Signal1<int,boost::shared_ptr<Playlist> >  AskAboutPlaylistDeletion;

	/** handlers should return 0 for "ignore the rate mismatch",
	    !0 for "do not use this session"
	*/
	static PBD::Signal2<int, framecnt_t, framecnt_t> AskAboutSampleRateMismatch;

	/** handlers should return !0 for use pending state, 0 for ignore it.
	*/
	static PBD::Signal0<int> AskAboutPendingState;

	boost::shared_ptr<AudioFileSource> create_audio_source_for_session (
		size_t, std::string const &, uint32_t, bool destructive, bool as_stub = false);
	
	boost::shared_ptr<MidiSource> create_midi_source_for_session (
		Track*, std::string const &, bool as_stub = false);

	boost::shared_ptr<Source> source_by_id (const PBD::ID&);
	boost::shared_ptr<Source> source_by_path_and_channel (const std::string&, uint16_t);
	uint32_t count_sources_by_origin (const std::string&);

	void add_playlist (boost::shared_ptr<Playlist>, bool unused = false);

	/* named selections */

	boost::shared_ptr<NamedSelection> named_selection_by_name (std::string name);
	void add_named_selection (boost::shared_ptr<NamedSelection>);
	void remove_named_selection (boost::shared_ptr<NamedSelection>);

	template<class T> void foreach_named_selection (T& obj, void (T::*func)(boost::shared_ptr<NamedSelection>));
	PBD::Signal0<void> NamedSelectionAdded;
	PBD::Signal0<void> NamedSelectionRemoved;

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

	PBD::Signal1<void,bool> AuditionActive;

	/* flattening stuff */

	boost::shared_ptr<Region> write_one_track (AudioTrack&, framepos_t start, framepos_t end,
			bool overwrite, std::vector<boost::shared_ptr<Source> >&, InterThreadInfo& wot,
			bool enable_processing = true);
	int freeze_all (InterThreadInfo&);

	/* session-wide solo/mute/rec-enable */

	bool soloing() const { return _non_soloed_outs_muted; }
	bool listening() const { return _listen_cnt > 0; }
	bool solo_isolated() const { return _solo_isolated_cnt > 0; }

	static const SessionEvent::RTeventCallback rt_cleanup;

	void set_solo (boost::shared_ptr<RouteList>, bool, SessionEvent::RTeventCallback after = rt_cleanup, bool group_override = false);
	void set_just_one_solo (boost::shared_ptr<Route>, bool, SessionEvent::RTeventCallback after = rt_cleanup);
	void set_mute (boost::shared_ptr<RouteList>, bool, SessionEvent::RTeventCallback after = rt_cleanup, bool group_override = false);
	void set_listen (boost::shared_ptr<RouteList>, bool, SessionEvent::RTeventCallback after = rt_cleanup, bool group_override = false);
	void set_record_enabled (boost::shared_ptr<RouteList>, bool, SessionEvent::RTeventCallback after = rt_cleanup, bool group_override = false);
	void set_solo_isolated (boost::shared_ptr<RouteList>, bool, SessionEvent::RTeventCallback after = rt_cleanup, bool group_override = false);

	PBD::Signal1<void,bool> SoloActive;
	PBD::Signal0<void> SoloChanged;
	PBD::Signal0<void> IsolatedChanged;
	
	/* control/master out */

	boost::shared_ptr<Route> monitor_out() const { return _monitor_out; }
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
	void unmark_send_id (uint32_t);
	void unmark_return_id (uint32_t);
	void unmark_insert_id (uint32_t);

	/* s/w "RAID" management */

	framecnt_t available_capture_duration();

	/* I/O bundles */

	void add_bundle (boost::shared_ptr<Bundle>);
	void remove_bundle (boost::shared_ptr<Bundle>);
	boost::shared_ptr<Bundle> bundle_by_name (std::string) const;

	PBD::Signal1<void,boost::shared_ptr<Bundle> > BundleAdded;
	PBD::Signal1<void,boost::shared_ptr<Bundle> > BundleRemoved;

	void midi_panic ();

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

	UndoTransaction* start_reversible_command (const std::string& cmd_name);
	void finish_reversible_command (UndoTransaction&);

	void add_command (Command *const cmd) {
		assert(!_current_trans.empty ());
		_current_trans.top()->add_command (cmd);
	}

	void add_commands (std::vector<Command*> const & cmds);

	std::map<PBD::ID,PBD::StatefulDestructible*> registry;

	// these commands are implemented in libs/ardour/session_command.cc
	Command* memento_command_factory(XMLNode* n);
	Command* stateful_diff_command_factory (XMLNode *);
	void register_with_memento_command_factory(PBD::ID, PBD::StatefulDestructible*);

	/* clicking */

	boost::shared_ptr<IO> click_io() { return _click_io; }

	/* disk, buffer loads */

	uint32_t playback_load ();
	uint32_t capture_load ();

	/* ranges */

	void request_play_range (std::list<AudioRange>*, bool leave_rolling = false);
	bool get_play_range () const { return _play_range; }

	/* buffers for gain and pan */

	gain_t* gain_automation_buffer () const;
	pan_t** pan_automation_buffer () const;

	void ensure_buffer_set (BufferSet& buffers, const ChanCount& howmany);

	/* VST support */

	static long vst_callback (AEffect* effect,
			long opcode,
			long index,
			long value,
			void* ptr,
			float opt);

	static PBD::Signal0<void> SendFeedback;

	/* Speakers */

	Speakers& get_speakers ();

	/* Controllables */

	boost::shared_ptr<PBD::Controllable> controllable_by_id (const PBD::ID&);
	boost::shared_ptr<PBD::Controllable> controllable_by_descriptor (const PBD::ControllableDescriptor&);

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
		PostTransportClearSubstate      = 0x80000,
		PostTransportAdjustPlaybackBuffering  = 0x100000,
		PostTransportAdjustCaptureBuffering   = 0x200000
	};

	enum SlaveState {
		Stopped,
		Waiting,
		Running
	};
	
	SlaveState slave_state() const { return _slave_state; }

	boost::shared_ptr<SessionPlaylists> playlists;

	void send_mmc_locate (framepos_t);
	int send_full_time_code (framepos_t);

	PBD::Signal0<void> RouteOrderKeyChanged;

	bool step_editing() const { return (_step_editors > 0); }

	void request_suspend_timecode_transmission ();
	void request_resume_timecode_transmission ();
	bool timecode_transmission_suspended () const;

	std::string source_search_path(DataType) const;
	void ensure_search_path_includes (const std::string& path, DataType type);

	std::list<std::string> unknown_processors () const;

	/* handlers can return an integer value:
	   0: config.set_audio_search_path() or config.set_midi_search_path() was used
	   to modify the search path and we should try to find it again.
	   1: quit entire session load
	   2: as 0, but don't ask about other missing files
	   3: don't ask about other missing files, and just mark this one missing
	   -1: just mark this one missing
	   any other value: as -1
	*/
	static PBD::Signal3<int,Session*,std::string,DataType> MissingFile;

	/** Emitted when the session wants Ardour to quit */
	static PBD::Signal0<void> Quit;

  protected:
	friend class AudioEngine;
	void set_block_size (pframes_t nframes);
	void set_frame_rate (framecnt_t nframes);

  protected:
	friend class Route;
	void schedule_curve_reallocation ();
	void update_latency_compensation (bool, bool);

  private:
	int  create (const std::string& mix_template, BusProfile*);
	void destroy ();

	enum SubState {
		PendingDeclickIn   = 0x1,
		PendingDeclickOut  = 0x2,
		StopPendingCapture = 0x4,
		PendingLocate      = 0x20,
	};

	/* stuff used in process() should be close together to
	   maximise cache hits
	*/

	typedef void (Session::*process_function_type)(pframes_t);

	AudioEngine&            _engine;
	mutable gint             processing_prohibited;
	process_function_type    process_function;
	process_function_type    last_process_function;
	bool                     waiting_for_sync_offset;
	framecnt_t              _base_frame_rate;
	framecnt_t              _current_frame_rate;  //this includes video pullup offset
	framecnt_t              _nominal_frame_rate;  //ignores audioengine setting, "native" SR
	int                      transport_sub_state;
	mutable gint            _record_status;
	framepos_t              _transport_frame;
	Location*               _session_range_location; ///< session range, or 0 if there is nothing in the session yet
	Slave*                  _slave;
	bool                    _silent;
	
	// varispeed playback
	double                  _transport_speed;
	double                  _last_transport_speed;
	double                  _target_transport_speed;
	CubicInterpolation       interpolation;

	bool                     auto_play_legal;
	framepos_t              _last_slave_transport_frame;
	framecnt_t               maximum_output_latency;
	framepos_t              _requested_return_frame;
	pframes_t                current_block_size;
	framecnt_t              _worst_output_latency;
	framecnt_t              _worst_input_latency;
	framecnt_t              _worst_track_latency;
	bool                    _have_captured;
	float                   _meter_hold;
	float                   _meter_falloff;
	bool                    _non_soloed_outs_muted;
	uint32_t                _listen_cnt;
	uint32_t                _solo_isolated_cnt;
	bool                    _writable;
	bool                    _was_seamless;

	void set_worst_io_latencies ();
	void set_worst_io_latencies_x (IOChange, void *) {
		set_worst_io_latencies ();
	}

	void update_latency_compensation_proxy (void* ignored);

	void ensure_buffers (ChanCount howmany = ChanCount::ZERO);

	void process_scrub          (pframes_t);
	void process_without_events (pframes_t);
	void process_with_events    (pframes_t);
	void process_audition       (pframes_t);
	void process_export         (pframes_t);
	int  process_export_fw      (pframes_t);

	void block_processing() { g_atomic_int_set (&processing_prohibited, 1); }
	void unblock_processing() { g_atomic_int_set (&processing_prohibited, 0); }
	bool processing_blocked() const { return g_atomic_int_get (&processing_prohibited); }

	Glib::Mutex                process_thread_lock;
	std::list<ProcessThread*>  process_threads;

	/* slave tracking */

	static const int delta_accumulator_size = 25;
	int delta_accumulator_cnt;
	int32_t delta_accumulator[delta_accumulator_size];
	int32_t average_slave_delta;
	int  average_dir;
	bool have_first_delta_accumulator;

	SlaveState _slave_state;
	framepos_t slave_wait_end;

	void reset_slave_state ();
	bool follow_slave (pframes_t);
	void calculate_moving_average_of_slave_delta (int dir, framecnt_t this_delta);
	void track_slave_state (float slave_speed, framepos_t slave_transport_frame, framecnt_t this_delta);
	void follow_slave_silently (pframes_t nframes, float slave_speed);

	void switch_to_sync_source (SyncSource); /* !RT context */
	void drop_sync_source ();  /* !RT context */
	void use_sync_source (Slave*); /* RT context */

	bool post_export_sync;
	framepos_t post_export_position;

	bool _exporting;
	bool _export_rolling;

	boost::shared_ptr<ExportHandler> export_handler;
	boost::shared_ptr<ExportStatus>  export_status;

	int  pre_export ();
	int  stop_audio_export ();
	void finalize_audio_export ();

	PBD::ScopedConnection export_freewheel_connection;

	void get_track_statistics ();
	int  process_routes (pframes_t, bool& need_butler);
	int  silent_process_routes (pframes_t, bool& need_butler);

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

	bool maybe_stop (framepos_t limit);
	bool maybe_sync_start (pframes_t &);

	void check_declick_out ();

	std::string             _path;
	std::string             _name;
	bool                    _is_new;
	bool                     session_send_mtc;
	bool                     session_midi_feedback;
	bool                     play_loop;
	bool                     loop_changing;
	framepos_t               last_loopend;

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

	framepos_t _last_roll_location;
	framepos_t _last_roll_or_reversal_location;
	framepos_t _last_record_location;

	bool              pending_locate_roll;
	framepos_t        pending_locate_frame;
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

	void schedule_playback_buffering_adjustment ();
	void schedule_capture_buffering_adjustment ();

	uint32_t    cumulative_rf_motion;
	uint32_t    rf_scale;

	void set_rf_speed (float speed);
	void reset_rf_scale (framecnt_t frames_moved);

	Locations*       _locations;
	void              locations_changed ();
	void              locations_added (Location*);
	void              handle_locations_changed (Locations::LocationList&);

	PBD::ScopedConnectionList punch_connections;
	void             auto_punch_start_changed (Location *);
	void             auto_punch_end_changed (Location *);
	void             auto_punch_changed (Location *);

	PBD::ScopedConnectionList loop_connections;
	void             auto_loop_changed (Location *);

	void first_stage_init (std::string path, std::string snapshot_name);
	int  second_stage_init ();
	void update_session_range_location_marker ();
	void remove_empty_sounds ();

	void setup_midi_control ();
	int  midi_read (MIDI::Port *);

	void enable_record ();

	void increment_transport_position (framecnt_t val) {
		if (max_framepos - val < _transport_frame) {
			_transport_frame = max_framepos;
		} else {
			_transport_frame += val;
		}
	}

	void decrement_transport_position (framecnt_t val) {
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

	void process_event (SessionEvent*);
	void set_next_event ();
	void cleanup_event (SessionEvent*,int);

	/* MIDI Machine Control */

	void spp_start (MIDI::Parser&, framepos_t timestamp);
	void spp_continue (MIDI::Parser&, framepos_t timestamp);
	void spp_stop (MIDI::Parser&, framepos_t timestamp);

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

	typedef boost::function<bool()> MidiTimeoutCallback;
	typedef std::list<MidiTimeoutCallback> MidiTimeoutList;

	MidiTimeoutList midi_timeouts;
	bool mmc_step_timeout ();

	MIDI::byte mtc_msg[16];
	MIDI::byte mtc_timecode_bits;   /* encoding of SMTPE type for MTC */
	MIDI::byte midi_msg[16];
	framepos_t outbound_mtc_timecode_frame;
	Timecode::Time transmitting_timecode_time;
	int next_quarter_frame_to_send;

	double _frames_per_timecode_frame; /* has to be floating point because of drop frame */
	framecnt_t _frames_per_hour;
	framecnt_t _timecode_frames_per_hour;

	/* cache the most-recently requested time conversions. This helps when we
	 * have multiple clocks showing the same time (e.g. the transport frame) */
	bool last_timecode_valid;
	framepos_t last_timecode_when;
	Timecode::Time last_timecode;

	bool _send_timecode_update; ///< Flag to send a full frame (Timecode) MTC message this cycle

	int send_midi_time_code_for_cycle (pframes_t nframes);

	void reset_record_status ();

	int no_roll (pframes_t nframes);
	int fail_roll (pframes_t nframes);

	bool non_realtime_work_pending() const { return static_cast<bool>(post_transport_work()); }
	bool process_can_proceed() const { return !(post_transport_work() & ProcessCannotProceedMask); }

	MidiControlUI* midi_control_ui;

	int           start_midi_thread ();
	void          terminate_midi_thread ();

	void set_play_loop (bool yn);
	void unset_play_loop ();
	void overwrite_some_buffers (Track *);
	void flush_all_inserts ();
	int  micro_locate (framecnt_t distance);
	void locate (framepos_t, bool with_roll, bool with_flush, bool with_loop=false, bool force=false, bool with_mmc=true);
	void start_locate (framepos_t, bool with_roll, bool with_flush, bool with_loop=false, bool force=false);
	void force_locate (framepos_t frame, bool with_roll = false);
	void set_track_speed (Track *, double speed);
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
	void          tempo_map_changed (const PBD::PropertyChange&);

	/* edit/mix groups */

	int load_route_groups (const XMLNode&, int);

	std::list<RouteGroup *> _route_groups;
	RouteGroup*             _all_route_group;

	/* routes stuff */

	boost::shared_ptr<Graph> route_graph;

	SerializedRCUManager<RouteList>  routes;

	void add_routes (RouteList&, bool save);
	uint32_t destructive_index;

	boost::shared_ptr<Route> XMLRouteFactory (const XMLNode&, int);
	boost::shared_ptr<Route> XMLRouteFactory_2X (const XMLNode&, int);

	void route_processors_changed (RouteProcessorChange);

	bool find_route_name (const char* base, uint32_t& id, char* name, size_t name_len);
	void count_existing_route_channels (ChanCount& in, ChanCount& out);
	void auto_connect_route (
		Route* route,
		ChanCount& existing_inputs,
		ChanCount& existing_outputs,
		bool connect_inputs = true,
		ChanCount input_start = ChanCount (),
		ChanCount output_start = ChanCount ()
		);

	/* mixer stuff */

	bool solo_update_disabled;

	void route_listen_changed (void *src, boost::weak_ptr<Route>);
	void route_mute_changed (void *src);
	void route_solo_changed (bool self_solo_change, void *src, boost::weak_ptr<Route>);
	void route_solo_isolated_changed (void *src, boost::weak_ptr<Route>);
	void update_route_solo_state (boost::shared_ptr<RouteList> r = boost::shared_ptr<RouteList>());

	void listen_position_changed ();
	void solo_control_mode_changed ();

	/* REGION MANAGEMENT */

	mutable Glib::Mutex region_lock;

	int load_regions (const XMLNode& node);

	void route_group_changed ();

	/* SOURCES */

	mutable Glib::Mutex source_lock;

  public:	
	typedef std::map<PBD::ID,boost::shared_ptr<Source> > SourceMap;

  private:	
	SourceMap sources;

  public:
	SourceMap get_sources() { return sources; }

  private:
	int load_sources (const XMLNode& node);
	XMLNode& get_sources_as_xml ();

	boost::shared_ptr<Source> XMLSourceFactory (const XMLNode&);

	/* PLAYLISTS */

	void remove_playlist (boost::weak_ptr<Playlist>);
	void track_playlist_changed (boost::weak_ptr<Track>);

	/* NAMED SELECTIONS */

	mutable Glib::Mutex named_selection_lock;
	typedef std::set<boost::shared_ptr<NamedSelection> > NamedSelectionList;
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

	int flatten_one_track (AudioTrack&, framepos_t start, framecnt_t cnt);

	/* INSERT AND SEND MANAGEMENT */

	boost::dynamic_bitset<uint32_t> send_bitset;
	boost::dynamic_bitset<uint32_t> return_bitset;
	boost::dynamic_bitset<uint32_t> insert_bitset;

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

	bool no_questions_about_missing_files;

	std::string get_best_session_directory_for_new_source ();

	mutable gint _playback_load;
	mutable gint _capture_load;

	/* I/O bundles */

	SerializedRCUManager<BundleList> _bundles;
	XMLNode* _bundle_xml_node;
	int load_bundles (XMLNode const &);

	void reverse_track_buffers ();

	UndoHistory                  _history;
	std::stack<UndoTransaction*> _current_trans;

	void jack_timebase_callback (jack_transport_state_t, pframes_t, jack_position_t*, int);
	int  jack_sync_callback (jack_transport_state_t, jack_position_t*);
	void reset_jack_connection (jack_client_t* jack);
	void process_rtop (SessionEvent*);

	XMLNode& state(bool);

	/* click track */

	Clicks                 clicks;
	bool                  _clicking;
	boost::shared_ptr<IO> _click_io;
	Sample*                click_data;
	Sample*                click_emphasis_data;
	framecnt_t             click_length;
	framecnt_t             click_emphasis_length;
	mutable Glib::RWLock   click_lock;

	static const Sample     default_click[];
	static const framecnt_t default_click_length;
	static const Sample     default_click_emphasis[];
	static const framecnt_t default_click_emphasis_length;

	Click *get_click();
	void   setup_click_sounds (int which);
	void   clear_clicks ();
	void   click (framepos_t start, framecnt_t nframes);

	std::vector<Route*> master_outs;

	/* range playback */

	std::list<AudioRange> current_audio_range;
	bool _play_range;
	void set_play_range (std::list<AudioRange>&, bool leave_rolling);
	void unset_play_range ();

	/* main outs */
	uint32_t main_outs;

	boost::shared_ptr<Route> _master_out;
	boost::shared_ptr<Route> _monitor_out;

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

	ChanCount n_physical_outputs;
	ChanCount n_physical_inputs;

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

	void update_have_rec_enabled_track ();
	gint _have_rec_enabled_track;

	static int ask_about_playlist_deletion (boost::shared_ptr<Playlist>);

	/* realtime "apply to set of routes" operations */
	SessionEvent* get_rt_event (
		boost::shared_ptr<RouteList> rl, bool yn,
		SessionEvent::RTeventCallback after, bool group_override, 
		void (Session::*method) (boost::shared_ptr<RouteList>, bool, bool));

	void rt_set_solo (boost::shared_ptr<RouteList>, bool yn, bool group_override);
	void rt_set_just_one_solo (boost::shared_ptr<RouteList>, bool yn, bool /* ignored*/ );
	void rt_set_mute (boost::shared_ptr<RouteList>, bool yn, bool group_override);
	void rt_set_listen (boost::shared_ptr<RouteList>, bool yn, bool group_override);
	void rt_set_solo_isolated (boost::shared_ptr<RouteList>, bool yn, bool group_override);
	void rt_set_record_enabled (boost::shared_ptr<RouteList>, bool yn, bool group_override);

	/** temporary list of Diskstreams used only during load of 2.X sessions */
	std::list<boost::shared_ptr<Diskstream> > _diskstreams_2X;

	void add_session_range_location (framepos_t, framepos_t);

	void setup_midi_machine_control ();
	void cleanup_stubfiles ();

	void route_order_key_changed ();

	void step_edit_status_change (bool);
	uint32_t _step_editors;

	/** true if timecode transmission by the transport is suspended, otherwise false */
	mutable gint _suspend_timecode_transmission;

	void update_locations_after_tempo_map_change (Locations::LocationList &);

	void start_time_changed (framepos_t);
	void end_time_changed (framepos_t);

	Speakers* _speakers; 
};

} // namespace ARDOUR

#endif /* __ardour_session_h__ */
