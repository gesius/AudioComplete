/*
    Copyright (C) 1999-2004 Paul Davis

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
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdio> /* sprintf(3) ... grrr */
#include <cmath>
#include <cerrno>
#include <unistd.h>
#include <limits.h>

#include <sigc++/bind.h>
#include <sigc++/retype.h>

#include <glibmm/thread.h>
#include <glibmm/miscutils.h>
#include <glibmm/fileutils.h>

#include "pbd/error.h"
#include <glibmm/thread.h>
#include "pbd/pathscanner.h"
#include "pbd/stl_delete.h"
#include "pbd/basename.h"
#include "pbd/stacktrace.h"
#include "pbd/file_utils.h"

#include "ardour/analyser.h"
#include "ardour/audio_buffer.h"
#include "ardour/audio_diskstream.h"
#include "ardour/audio_track.h"
#include "ardour/audioengine.h"
#include "ardour/audiofilesource.h"
#include "ardour/audioplaylist.h"
#include "ardour/audioregion.h"
#include "ardour/auditioner.h"
#include "ardour/buffer_set.h"
#include "ardour/bundle.h"
#include "ardour/click.h"
#include "ardour/configuration.h"
#include "ardour/crossfade.h"
#include "ardour/cycle_timer.h"
#include "ardour/data_type.h"
#include "ardour/filename_extensions.h"
#include "ardour/io_processor.h"
#include "ardour/midi_diskstream.h"
#include "ardour/midi_playlist.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/named_selection.h"
#include "ardour/playlist.h"
#include "ardour/plugin_insert.h"
#include "ardour/port_insert.h"
#include "ardour/processor.h"
#include "ardour/recent_sessions.h"
#include "ardour/region_factory.h"
#include "ardour/return.h"
#include "ardour/route_group.h"
#include "ardour/send.h"
#include "ardour/session.h"
#include "ardour/session_directory.h"
#include "ardour/session_directory.h"
#include "ardour/session_metadata.h"
#include "ardour/slave.h"
#include "ardour/smf_source.h"
#include "ardour/source_factory.h"
#include "ardour/tape_file_matcher.h"
#include "ardour/tempo.h"
#include "ardour/utils.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using boost::shared_ptr;

bool Session::_disable_all_loaded_plugins = false;

sigc::signal<void,std::string> Session::Dialog;
sigc::signal<int> Session::AskAboutPendingState;
sigc::signal<int,nframes_t,nframes_t> Session::AskAboutSampleRateMismatch;
sigc::signal<void> Session::SendFeedback;

sigc::signal<void> Session::SMPTEOffsetChanged;
sigc::signal<void> Session::StartTimeChanged;
sigc::signal<void> Session::EndTimeChanged;
sigc::signal<void> Session::AutoBindingOn;
sigc::signal<void> Session::AutoBindingOff;
sigc::signal<void, std::string, std::string> Session::Exported;

Session::Session (AudioEngine &eng,
		  const string& fullpath,
		  const string& snapshot_name,
		  string mix_template)

	: _engine (eng),
	  _target_transport_speed (0.0),
	  _requested_return_frame (-1),
	  _scratch_buffers(new BufferSet()),
	  _silent_buffers(new BufferSet()),
	  _mix_buffers(new BufferSet()),
	  mmc (0),
	  _mmc_port (default_mmc_port),
	  _mtc_port (default_mtc_port),
	  _midi_port (default_midi_port),
	  _midi_clock_port (default_midi_clock_port),
	  _session_dir (new SessionDirectory(fullpath)),
	  pending_events (2048),
	  state_tree (0),
	  butler_mixdown_buffer (0),
	  butler_gain_buffer (0),
	  post_transport_work((PostTransportWork)0),
	  _send_smpte_update (false),
	  midi_thread (pthread_t (0)),
	  midi_requests (128), // the size of this should match the midi request pool size
	  diskstreams (new DiskstreamList),
	  routes (new RouteList),
	  _total_free_4k_blocks (0),
	  _bundles (new BundleList),
	  _bundle_xml_node (0),
	  _click_io ((IO*) 0),
	  click_data (0),
	  click_emphasis_data (0),
	  main_outs (0),
	  _metadata (new SessionMetadata()),
	  _have_rec_enabled_diskstream (false)

{
	bool new_session;

	if (!eng.connected()) {
		throw failed_constructor();
	}

	cerr << "Loading session " << fullpath << " using snapshot " << snapshot_name << " (1)" << endl;

	n_physical_outputs = _engine.n_physical_outputs(DataType::AUDIO);
	n_physical_inputs =  _engine.n_physical_inputs(DataType::AUDIO);

	first_stage_init (fullpath, snapshot_name);

	new_session = !Glib::file_test (_path, Glib::FileTest (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR));

	if (new_session) {
		if (create (new_session, mix_template, compute_initial_length())) {
			destroy ();
			throw failed_constructor ();
		}
	}

	if (second_stage_init (new_session)) {
		destroy ();
		throw failed_constructor ();
	}

	store_recent_sessions(_name, _path);

	bool was_dirty = dirty();

	_state_of_the_state = StateOfTheState (_state_of_the_state & ~Dirty);

	Config->ParameterChanged.connect (bind (mem_fun (*this, &Session::config_changed), false));
	config.ParameterChanged.connect (bind (mem_fun (*this, &Session::config_changed), true));

	if (was_dirty) {
		DirtyChanged (); /* EMIT SIGNAL */
	}
}

Session::Session (AudioEngine &eng,
		  string fullpath,
		  string snapshot_name,
		  AutoConnectOption input_ac,
		  AutoConnectOption output_ac,
		  uint32_t control_out_channels,
		  uint32_t master_out_channels,
		  uint32_t requested_physical_in,
		  uint32_t requested_physical_out,
		  nframes_t initial_length)

	: _engine (eng),
	  _target_transport_speed (0.0),
	  _requested_return_frame (-1),
	  _scratch_buffers(new BufferSet()),
	  _silent_buffers(new BufferSet()),
	  _mix_buffers(new BufferSet()),
	  mmc (0),
	  _mmc_port (default_mmc_port),
	  _mtc_port (default_mtc_port),
	  _midi_port (default_midi_port),
	  _midi_clock_port (default_midi_clock_port),
	  _session_dir ( new SessionDirectory(fullpath)),
	  pending_events (2048),
	  state_tree (0),
	  butler_mixdown_buffer (0),
	  butler_gain_buffer (0),
	  post_transport_work((PostTransportWork)0),
	  _send_smpte_update (false),
	  midi_thread (pthread_t (0)),
	  midi_requests (16),
	  diskstreams (new DiskstreamList),
	  routes (new RouteList),
	  _total_free_4k_blocks (0),
	  _bundles (new BundleList),
	  _bundle_xml_node (0),
	  _click_io ((IO *) 0),
	  click_data (0),
	  click_emphasis_data (0),
	  main_outs (0),
	  _metadata (new SessionMetadata()),
	  _have_rec_enabled_diskstream (false)
{
	bool new_session;

	if (!eng.connected()) {
		throw failed_constructor();
	}

	cerr << "Loading session " << fullpath << " using snapshot " << snapshot_name << " (2)" << endl;

	n_physical_outputs = _engine.n_physical_outputs (DataType::AUDIO);
	n_physical_inputs = _engine.n_physical_inputs (DataType::AUDIO);

	if (n_physical_inputs) {
		n_physical_inputs = max (requested_physical_in, n_physical_inputs);
	}

	if (n_physical_outputs) {
		n_physical_outputs = max (requested_physical_out, n_physical_outputs);
	}

	first_stage_init (fullpath, snapshot_name);

	new_session = !g_file_test (_path.c_str(), GFileTest (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR));

	if (new_session) {
		if (create (new_session, string(), initial_length)) {
			destroy ();
			throw failed_constructor ();
		}
	}

	{
		/* set up Master Out and Control Out if necessary */

		RouteList rl;
		int control_id = 1;

		if (control_out_channels) {
			ChanCount count(DataType::AUDIO, control_out_channels);
			shared_ptr<Route> r (new Route (*this, _("monitor"), Route::ControlOut, DataType::AUDIO));
			r->input()->ensure_io (count, false, this);
			r->output()->ensure_io (count, false, this);
			r->set_remote_control_id (control_id++);

			rl.push_back (r);
		}

		if (master_out_channels) {
			ChanCount count(DataType::AUDIO, master_out_channels);
			shared_ptr<Route> r (new Route (*this, _("master"), Route::MasterOut, DataType::AUDIO));
			r->input()->ensure_io (count, false, this);
			r->output()->ensure_io (count, false, this);
			r->set_remote_control_id (control_id);

			rl.push_back (r);
		} else {
			/* prohibit auto-connect to master, because there isn't one */
			output_ac = AutoConnectOption (output_ac & ~AutoConnectMaster);
		}

		if (!rl.empty()) {
			add_routes (rl, false);
		}

	}

	if (no_auto_connect()) {
		input_ac = AutoConnectOption (0);
		output_ac = AutoConnectOption (0);
	}

	Config->set_input_auto_connect (input_ac);
	Config->set_output_auto_connect (output_ac);

	if (second_stage_init (new_session)) {
		destroy ();
		throw failed_constructor ();
	}

	store_recent_sessions (_name, _path);

	_state_of_the_state = StateOfTheState (_state_of_the_state & ~Dirty);

	Config->ParameterChanged.connect (bind (mem_fun (*this, &Session::config_changed), false));
}

Session::~Session ()
{
	destroy ();
}

void
Session::destroy ()
{
	/* if we got to here, leaving pending capture state around
	   is a mistake.
	*/

	remove_pending_capture_state ();

	_state_of_the_state = StateOfTheState (CannotSave|Deletion);

	_engine.remove_session ();

	GoingAway (); /* EMIT SIGNAL */

	/* do this */

	notify_callbacks ();

	/* clear history so that no references to objects are held any more */

	_history.clear ();

	/* clear state tree so that no references to objects are held any more */

	delete state_tree;

	terminate_butler_thread ();
	//terminate_midi_thread ();

	if (click_data != default_click) {
		delete [] click_data;
	}

	if (click_emphasis_data != default_click_emphasis) {
		delete [] click_emphasis_data;
	}

	clear_clicks ();

	delete _scratch_buffers;
	delete _silent_buffers;
	delete _mix_buffers;

	AudioDiskstream::free_working_buffers();

	Route::SyncOrderKeys.clear();

#undef TRACK_DESTRUCTION
#ifdef TRACK_DESTRUCTION
	cerr << "delete named selections\n";
#endif /* TRACK_DESTRUCTION */
	for (NamedSelectionList::iterator i = named_selections.begin(); i != named_selections.end(); ) {
		NamedSelectionList::iterator tmp;

		tmp = i;
		++tmp;

		delete *i;
		i = tmp;
	}

#ifdef TRACK_DESTRUCTION
	cerr << "delete playlists\n";
#endif /* TRACK_DESTRUCTION */
	for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ) {
		PlaylistList::iterator tmp;

		tmp = i;
		++tmp;

		(*i)->drop_references ();

		i = tmp;
	}

	for (PlaylistList::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ) {
		PlaylistList::iterator tmp;

		tmp = i;
		++tmp;

		(*i)->drop_references ();

		i = tmp;
	}

	playlists.clear ();
	unused_playlists.clear ();

#ifdef TRACK_DESTRUCTION
	cerr << "delete regions\n";
#endif /* TRACK_DESTRUCTION */

	for (RegionList::iterator i = regions.begin(); i != regions.end(); ) {
		RegionList::iterator tmp;

		tmp = i;
		++tmp;

		i->second->drop_references ();

		i = tmp;
	}

	regions.clear ();

#ifdef TRACK_DESTRUCTION
	cerr << "delete routes\n";
#endif /* TRACK_DESTRUCTION */
	{
		RCUWriter<RouteList> writer (routes);
		boost::shared_ptr<RouteList> r = writer.get_copy ();
		for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
			(*i)->drop_references ();
		}
		r->clear ();
		/* writer goes out of scope and updates master */
	}

	routes.flush ();

#ifdef TRACK_DESTRUCTION
	cerr << "delete diskstreams\n";
#endif /* TRACK_DESTRUCTION */
       {
	       RCUWriter<DiskstreamList> dwriter (diskstreams);
	       boost::shared_ptr<DiskstreamList> dsl = dwriter.get_copy();
	       for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		       (*i)->drop_references ();
	       }
	       dsl->clear ();
       }
       diskstreams.flush ();

#ifdef TRACK_DESTRUCTION
	cerr << "delete audio sources\n";
#endif /* TRACK_DESTRUCTION */
	for (SourceMap::iterator i = sources.begin(); i != sources.end(); ) {
		SourceMap::iterator tmp;

		tmp = i;
		++tmp;

		i->second->drop_references ();

		i = tmp;
	}
	sources.clear ();

#ifdef TRACK_DESTRUCTION
	cerr << "delete mix groups\n";
#endif /* TRACK_DESTRUCTION */
	for (list<RouteGroup *>::iterator i = mix_groups.begin(); i != mix_groups.end(); ) {
		list<RouteGroup*>::iterator tmp;

		tmp = i;
		++tmp;

		delete *i;

		i = tmp;
	}

#ifdef TRACK_DESTRUCTION
	cerr << "delete edit groups\n";
#endif /* TRACK_DESTRUCTION */
	for (list<RouteGroup *>::iterator i = edit_groups.begin(); i != edit_groups.end(); ) {
		list<RouteGroup*>::iterator tmp;

		tmp = i;
		++tmp;

		delete *i;

		i = tmp;
	}

	delete [] butler_mixdown_buffer;
	delete [] butler_gain_buffer;

	Crossfade::set_buffer_size (0);

	delete mmc;
}

void
Session::set_worst_io_latencies ()
{
	_worst_output_latency = 0;
	_worst_input_latency = 0;

	if (!_engine.connected()) {
		return;
	}

	boost::shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		_worst_output_latency = max (_worst_output_latency, (*i)->output()->latency());
		_worst_input_latency = max (_worst_input_latency, (*i)->input()->latency());
	}
}

void
Session::when_engine_running ()
{
	string first_physical_output;

	BootMessage (_("Set block size and sample rate"));

	set_block_size (_engine.frames_per_cycle());
	set_frame_rate (_engine.frame_rate());

	BootMessage (_("Using configuration"));

	Config->map_parameters (bind (mem_fun (*this, &Session::config_changed), false));

	/* every time we reconnect, recompute worst case output latencies */

	_engine.Running.connect (mem_fun (*this, &Session::set_worst_io_latencies));

	if (synced_to_jack()) {
		_engine.transport_stop ();
	}

	if (config.get_jack_time_master()) {
		_engine.transport_locate (_transport_frame);
	}

	_clicking = false;

	try {
		XMLNode* child = 0;

		_click_io.reset (new ClickIO (*this, "click"));

		if (state_tree && (child = find_named_node (*state_tree->root(), "Click")) != 0) {

			/* existing state for Click */

			if (_click_io->set_state (*child->children().front()) == 0) {

				_clicking = Config->get_clicking ();

			} else {

				error << _("could not setup Click I/O") << endmsg;
				_clicking = false;
			}

		} else {

			/* default state for Click: dual-mono to first 2 physical outputs */

                       for (int physport = 0; physport < 2; ++physport) {
                               string physical_output = _engine.get_nth_physical_output (DataType::AUDIO, physport);
			       
                               if (physical_output.length()) {
                                       if (_click_io->add_port (physical_output, this)) {
                                               // relax, even though its an error
				       }
			       }
		       }
		       
                       if (_click_io->n_ports () > ChanCount::ZERO) {
                               _clicking = Config->get_clicking ();
                       }
		}
	}

	catch (failed_constructor& err) {
		error << _("cannot setup Click I/O") << endmsg;
	}

	BootMessage (_("Compute I/O Latencies"));

	set_worst_io_latencies ();

	if (_clicking) {
		// XXX HOW TO ALERT UI TO THIS ? DO WE NEED TO?
	}

	BootMessage (_("Set up standard connections"));

	/* Create a set of Bundle objects that map
	   to the physical I/O currently available.  We create both
	   mono and stereo bundles, so that the common cases of mono
	   and stereo tracks get bundles to put in their mixer strip
	   in / out menus.  There may be a nicer way of achieving that;
	   it doesn't really scale that well to higher channel counts 
	*/

	/* mono output bundles */

	for (uint32_t np = 0; np < n_physical_outputs; ++np) {
		char buf[32];
		snprintf (buf, sizeof (buf), _("out %" PRIu32), np+1);

 		shared_ptr<Bundle> c (new Bundle (buf, true));
		c->add_channel (_("mono"));
 		c->set_port (0, _engine.get_nth_physical_output (DataType::AUDIO, np));

 		add_bundle (c);
	}

	/* stereo output bundles */

	for (uint32_t np = 0; np < n_physical_outputs; np += 2) {
		if (np + 1 < n_physical_outputs) {
			char buf[32];
			snprintf (buf, sizeof(buf), _("out %" PRIu32 "+%" PRIu32), np + 1, np + 2);
			shared_ptr<Bundle> c (new Bundle (buf, true));
			c->add_channel (_("L"));
			c->set_port (0, _engine.get_nth_physical_output (DataType::AUDIO, np));
			c->add_channel (_("R"));
			c->set_port (1, _engine.get_nth_physical_output (DataType::AUDIO, np + 1));

			add_bundle (c);
		}
	}

	/* mono input bundles */

	for (uint32_t np = 0; np < n_physical_inputs; ++np) {
		char buf[32];
		snprintf (buf, sizeof (buf), _("in %" PRIu32), np+1);

 		shared_ptr<Bundle> c (new Bundle (buf, false));
		c->add_channel (_("mono"));
 		c->set_port (0, _engine.get_nth_physical_input (DataType::AUDIO, np));

 		add_bundle (c);
	}

	/* stereo input bundles */

	for (uint32_t np = 0; np < n_physical_inputs; np += 2) {
		if (np + 1 < n_physical_inputs) {
			char buf[32];
			snprintf (buf, sizeof(buf), _("in %" PRIu32 "+%" PRIu32), np + 1, np + 2);

			shared_ptr<Bundle> c (new Bundle (buf, false));
			c->add_channel (_("L"));
			c->set_port (0, _engine.get_nth_physical_input (DataType::AUDIO, np));
			c->add_channel (_("R"));
			c->set_port (1, _engine.get_nth_physical_input (DataType::AUDIO, np + 1));

			add_bundle (c);
		}
	}

	if (Config->get_auto_connect_standard_busses() && !no_auto_connect()) {

		if (_master_out) {
			
			/* if requested auto-connect the outputs to the first N physical ports.
			 */

			uint32_t limit = _master_out->n_outputs().n_total();
			
			for (uint32_t n = 0; n < limit; ++n) {
				Port* p = _master_out->output()->nth (n);
				string connect_to = _engine.get_nth_physical_output (DataType (p->type()), n);

				if (!connect_to.empty()) {
					if (_master_out->output()->connect (p, connect_to, this)) {
						error << string_compose (_("cannot connect master output %1 to %2"), n, connect_to) 
						      << endmsg;
						break;
					}
				}
			}
		}

		if (_control_out) {

			uint32_t limit = _control_out->n_outputs().n_total();
			
			for (uint32_t n = 0; n < limit; ++n) {
				Port* p = _control_out->output()->nth (n);
				string connect_to = _engine.get_nth_physical_output (DataType (p->type()), n);
				
				if (!connect_to.empty()) {
					if (_control_out->output()->connect (p, connect_to, this)) {
						error << string_compose (_("cannot connect control output %1 to %2"), n, connect_to) 
						      << endmsg;
						break;
					}
				}
			}
		}
	}

	BootMessage (_("Setup signal flow and plugins"));

	hookup_io ();

	/* catch up on send+insert cnts */

	_state_of_the_state = StateOfTheState (_state_of_the_state & ~(CannotSave|Dirty));

	/* hook us up to the engine */

	BootMessage (_("Connect to engine"));

	_engine.set_session (this);
}

void
Session::hookup_io ()
{
	/* stop graph reordering notifications from
	   causing resorts, etc.
	*/

	_state_of_the_state = StateOfTheState (_state_of_the_state | InitialConnecting);


	if (!auditioner) {

		/* we delay creating the auditioner till now because
		   it makes its own connections to ports.
		   the engine has to be running for this to work.
		*/

		try {
			auditioner.reset (new Auditioner (*this));
		}

		catch (failed_constructor& err) {
			warning << _("cannot create Auditioner: no auditioning of regions possible") << endmsg;
		}
	}

	/* load bundles, which we may have postponed earlier on */
	if (_bundle_xml_node) {
		load_bundles (*_bundle_xml_node);
		delete _bundle_xml_node;
	}

	/* Tell all IO objects to connect themselves together */

	IO::enable_connecting ();

	/* Now reset all panners */

	Delivery::reset_panners ();

	/* Connect tracks to listen/solo etc. busses XXX generalize this beyond control_out */

	if (_control_out) {

		boost::shared_ptr<RouteList> r = routes.reader ();
		
		for (RouteList::iterator x = r->begin(); x != r->end(); ++x) {

			boost::shared_ptr<Track> t = boost::dynamic_pointer_cast<Track> (*x);

			if (t) {
				t->listen_via (_control_out, X_("listen"));
			}
		}
	}

	/* Anyone who cares about input state, wake up and do something */

	IOConnectionsComplete (); /* EMIT SIGNAL */

	_state_of_the_state = StateOfTheState (_state_of_the_state & ~InitialConnecting);


	/* now handle the whole enchilada as if it was one
	   graph reorder event.
	*/

	graph_reordered ();

	/* update mixer solo state */

	catch_up_on_solo();
}

void
Session::playlist_length_changed ()
{
	/* we can't just increase end_location->end() if pl->get_maximum_extent()
	   if larger. if the playlist used to be the longest playlist,
	   and its now shorter, we have to decrease end_location->end(). hence,
	   we have to iterate over all diskstreams and check the
	   playlists currently in use.
	*/
	find_current_end ();
}

void
Session::diskstream_playlist_changed (boost::shared_ptr<Diskstream> dstream)
{
	boost::shared_ptr<Playlist> playlist;

	if ((playlist = dstream->playlist()) != 0) {
		playlist->LengthChanged.connect (mem_fun (this, &Session::playlist_length_changed));
	}

	/* see comment in playlist_length_changed () */
	find_current_end ();
}

bool
Session::record_enabling_legal () const
{
	/* this used to be in here, but survey says.... we don't need to restrict it */
 	// if (record_status() == Recording) {
 	//	return false;
 	// }

	if (Config->get_all_safe()) {
		return false;
	}
	return true;
}

void
Session::reset_input_monitor_state ()
{
	if (transport_rolling()) {

		boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

		for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
			if ((*i)->record_enabled ()) {
				//cerr << "switching to input = " << !auto_input << __FILE__ << __LINE__ << endl << endl;
				(*i)->monitor_input (Config->get_monitoring_model() == HardwareMonitoring && !config.get_auto_input());
			}
		}
	} else {
		boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

		for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
			if ((*i)->record_enabled ()) {
				//cerr << "switching to input = " << !Config->get_auto_input() << __FILE__ << __LINE__ << endl << endl;
				(*i)->monitor_input (Config->get_monitoring_model() == HardwareMonitoring);
			}
		}
	}
}

void
Session::auto_punch_start_changed (Location* location)
{
	replace_event (Event::PunchIn, location->start());

	if (get_record_enabled() && config.get_punch_in()) {
		/* capture start has been changed, so save new pending state */
		save_state ("", true);
	}
}

void
Session::auto_punch_end_changed (Location* location)
{
	nframes_t when_to_stop = location->end();
	// when_to_stop += _worst_output_latency + _worst_input_latency;
	replace_event (Event::PunchOut, when_to_stop);
}

void
Session::auto_punch_changed (Location* location)
{
	nframes_t when_to_stop = location->end();

	replace_event (Event::PunchIn, location->start());
	//when_to_stop += _worst_output_latency + _worst_input_latency;
	replace_event (Event::PunchOut, when_to_stop);
}

void
Session::auto_loop_changed (Location* location)
{
	replace_event (Event::AutoLoop, location->end(), location->start());

	if (transport_rolling() && play_loop) {

		//if (_transport_frame < location->start() || _transport_frame > location->end()) {

		if (_transport_frame > location->end()) {
			// relocate to beginning of loop
			clear_events (Event::LocateRoll);

			request_locate (location->start(), true);

		}
		else if (Config->get_seamless_loop() && !loop_changing) {

			// schedule a locate-roll to refill the diskstreams at the
			// previous loop end
			loop_changing = true;

			if (location->end() > last_loopend) {
				clear_events (Event::LocateRoll);
				Event *ev = new Event (Event::LocateRoll, Event::Add, last_loopend, last_loopend, 0, true);
				queue_event (ev);
			}

		}
	}

	last_loopend = location->end();
}

void
Session::set_auto_punch_location (Location* location)
{
	Location* existing;

	if ((existing = _locations.auto_punch_location()) != 0 && existing != location) {
		auto_punch_start_changed_connection.disconnect();
		auto_punch_end_changed_connection.disconnect();
		auto_punch_changed_connection.disconnect();
		existing->set_auto_punch (false, this);
		remove_event (existing->start(), Event::PunchIn);
		clear_events (Event::PunchOut);
		auto_punch_location_changed (0);
	}

	set_dirty();

	if (location == 0) {
		return;
	}

	if (location->end() <= location->start()) {
		error << _("Session: you can't use that location for auto punch (start <= end)") << endmsg;
		return;
	}

	auto_punch_start_changed_connection.disconnect();
	auto_punch_end_changed_connection.disconnect();
	auto_punch_changed_connection.disconnect();

	auto_punch_start_changed_connection = location->start_changed.connect (mem_fun (this, &Session::auto_punch_start_changed));
	auto_punch_end_changed_connection = location->end_changed.connect (mem_fun (this, &Session::auto_punch_end_changed));
	auto_punch_changed_connection = location->changed.connect (mem_fun (this, &Session::auto_punch_changed));

	location->set_auto_punch (true, this);


	auto_punch_changed (location);

	auto_punch_location_changed (location);
}

void
Session::set_auto_loop_location (Location* location)
{
	Location* existing;

	if ((existing = _locations.auto_loop_location()) != 0 && existing != location) {
		auto_loop_start_changed_connection.disconnect();
		auto_loop_end_changed_connection.disconnect();
		auto_loop_changed_connection.disconnect();
		existing->set_auto_loop (false, this);
		remove_event (existing->end(), Event::AutoLoop);
		auto_loop_location_changed (0);
	}

	set_dirty();

	if (location == 0) {
		return;
	}

	if (location->end() <= location->start()) {
		error << _("Session: you can't use a mark for auto loop") << endmsg;
		return;
	}

	last_loopend = location->end();

	auto_loop_start_changed_connection.disconnect();
	auto_loop_end_changed_connection.disconnect();
	auto_loop_changed_connection.disconnect();

	auto_loop_start_changed_connection = location->start_changed.connect (mem_fun (this, &Session::auto_loop_changed));
	auto_loop_end_changed_connection = location->end_changed.connect (mem_fun (this, &Session::auto_loop_changed));
	auto_loop_changed_connection = location->changed.connect (mem_fun (this, &Session::auto_loop_changed));

	location->set_auto_loop (true, this);

	/* take care of our stuff first */

	auto_loop_changed (location);

	/* now tell everyone else */

	auto_loop_location_changed (location);
}

void
Session::locations_added (Location* ignored)
{
	set_dirty ();
}

void
Session::locations_changed ()
{
	_locations.apply (*this, &Session::handle_locations_changed);
}

void
Session::handle_locations_changed (Locations::LocationList& locations)
{
	Locations::LocationList::iterator i;
	Location* location;
	bool set_loop = false;
	bool set_punch = false;

	for (i = locations.begin(); i != locations.end(); ++i) {

		location =* i;

		if (location->is_auto_punch()) {
			set_auto_punch_location (location);
			set_punch = true;
		}
		if (location->is_auto_loop()) {
			set_auto_loop_location (location);
			set_loop = true;
		}

		if (location->is_start()) {
			start_location = location;
		}
		if (location->is_end()) {
			end_location = location;
		}
	}

	if (!set_loop) {
		set_auto_loop_location (0);
	}
	if (!set_punch) {
		set_auto_punch_location (0);
	}

	set_dirty();
}

void
Session::enable_record ()
{
	/* XXX really atomic compare+swap here */
	if (g_atomic_int_get (&_record_status) != Recording) {
		g_atomic_int_set (&_record_status, Recording);
		_last_record_location = _transport_frame;
		deliver_mmc(MIDI::MachineControl::cmdRecordStrobe, _last_record_location);

		if (Config->get_monitoring_model() == HardwareMonitoring && config.get_auto_input()) {
			boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
			for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
				if ((*i)->record_enabled ()) {
					(*i)->monitor_input (true);
				}
			}
		}

		RecordStateChanged ();
	}
}

void
Session::disable_record (bool rt_context, bool force)
{
	RecordState rs;

	if ((rs = (RecordState) g_atomic_int_get (&_record_status)) != Disabled) {

		if ((!Config->get_latched_record_enable () && !play_loop) || force) {
			g_atomic_int_set (&_record_status, Disabled);
		} else {
			if (rs == Recording) {
				g_atomic_int_set (&_record_status, Enabled);
			}
		}

		// FIXME: timestamp correct? [DR]
		// FIXME FIXME FIXME: rt_context?  this must be called in the process thread.
		// does this /need/ to be sent in all cases?
		if (rt_context)
			deliver_mmc (MIDI::MachineControl::cmdRecordExit, _transport_frame);

		if (Config->get_monitoring_model() == HardwareMonitoring && config.get_auto_input()) {
			boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

			for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
				if ((*i)->record_enabled ()) {
					(*i)->monitor_input (false);
				}
			}
		}

		RecordStateChanged (); /* emit signal */

		if (!rt_context) {
			remove_pending_capture_state ();
		}
	}
}

void
Session::step_back_from_record ()
{
	/* XXX really atomic compare+swap here */
	if (g_atomic_int_get (&_record_status) == Recording) {
		g_atomic_int_set (&_record_status, Enabled);

		if (Config->get_monitoring_model() == HardwareMonitoring && config.get_auto_input()) {
			boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

			for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
				if ((*i)->record_enabled ()) {
					//cerr << "switching from input" << __FILE__ << __LINE__ << endl << endl;
					(*i)->monitor_input (false);
				}
			}
		}
	}
}

void
Session::maybe_enable_record ()
{
	g_atomic_int_set (&_record_status, Enabled);

	/* this function is currently called from somewhere other than an RT thread.
	   this save_state() call therefore doesn't impact anything.
	*/

	save_state ("", true);

	if (_transport_speed) {
		if (!config.get_punch_in()) {
			enable_record ();
		}
	} else {
		deliver_mmc (MIDI::MachineControl::cmdRecordPause, _transport_frame);
		RecordStateChanged (); /* EMIT SIGNAL */
	}

	set_dirty();
}

nframes_t
Session::audible_frame () const
{
	nframes_t ret;
	nframes_t offset;
	nframes_t tf;

	/* the first of these two possible settings for "offset"
	   mean that the audible frame is stationary until
	   audio emerges from the latency compensation
	   "pseudo-pipeline".

	   the second means that the audible frame is stationary
	   until audio would emerge from a physical port
	   in the absence of any plugin latency compensation
	*/

	offset = _worst_output_latency;

	if (offset > current_block_size) {
		offset -= current_block_size;
	} else {
		/* XXX is this correct? if we have no external
		   physical connections and everything is internal
		   then surely this is zero? still, how
		   likely is that anyway?
		*/
		offset = current_block_size;
	}

	if (synced_to_jack()) {
		tf = _engine.transport_frame();
	} else {
		tf = _transport_frame;
	}
	
	ret = tf;

	if (!non_realtime_work_pending()) {

		/* MOVING */

		/* check to see if we have passed the first guaranteed
		   audible frame past our last start position. if not,
		   return that last start point because in terms
		   of audible frames, we have not moved yet.
		*/

		if (_transport_speed > 0.0f) {

			if (!play_loop || !have_looped) {
				if (tf < _last_roll_location + offset) {
					return _last_roll_location;
				}
			} 
			

			/* forwards */
			ret -= offset;

		} else if (_transport_speed < 0.0f) {

			/* XXX wot? no backward looping? */

			if (tf > _last_roll_location - offset) {
				return _last_roll_location;
			} else {
				/* backwards */
				ret += offset;
			}
		}
	}

	return ret;
}

void
Session::set_frame_rate (nframes_t frames_per_second)
{
	/** \fn void Session::set_frame_size(nframes_t)
		the AudioEngine object that calls this guarantees
		that it will not be called while we are also in
		::process(). Its fine to do things that block
		here.
	*/

	_base_frame_rate = frames_per_second;

	sync_time_vars();

	Automatable::set_automation_interval ((jack_nframes_t) ceil ((double) frames_per_second * (0.001 * Config->get_automation_interval())));

	clear_clicks ();

	// XXX we need some equivalent to this, somehow
	// SndFileSource::setup_standard_crossfades (frames_per_second);

	set_dirty();

	/* XXX need to reset/reinstantiate all LADSPA plugins */
}

void
Session::set_block_size (nframes_t nframes)
{
	/* the AudioEngine guarantees
	   that it will not be called while we are also in
	   ::process(). It is therefore fine to do things that block
	   here.
	*/

	{
		current_block_size = nframes;

		ensure_buffers(_scratch_buffers->available());

		delete [] _gain_automation_buffer;
		_gain_automation_buffer = new gain_t[nframes];

		allocate_pan_automation_buffers (nframes, _npan_buffers, true);

		boost::shared_ptr<RouteList> r = routes.reader ();

		for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
			(*i)->set_block_size (nframes);
		}

		boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();
		for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
			(*i)->set_block_size (nframes);
		}

		set_worst_io_latencies ();
	}
}

void
Session::set_default_fade (float steepness, float fade_msecs)
{
#if 0
	nframes_t fade_frames;

	/* Don't allow fade of less 1 frame */

	if (fade_msecs < (1000.0 * (1.0/_current_frame_rate))) {

		fade_msecs = 0;
		fade_frames = 0;

	} else {

		fade_frames = (nframes_t) floor (fade_msecs * _current_frame_rate * 0.001);

	}

	default_fade_msecs = fade_msecs;
	default_fade_steepness = steepness;

	{
		// jlc, WTF is this!
		Glib::RWLock::ReaderLock lm (route_lock);
		AudioRegion::set_default_fade (steepness, fade_frames);
	}

	set_dirty();

	/* XXX have to do this at some point */
	/* foreach region using default fade, reset, then
	   refill_all_diskstream_buffers ();
	*/
#endif
}

struct RouteSorter {
    bool operator() (boost::shared_ptr<Route> r1, boost::shared_ptr<Route> r2) {
	    if (r1->fed_by.find (r2) != r1->fed_by.end()) {
		    return false;
	    } else if (r2->fed_by.find (r1) != r2->fed_by.end()) {
		    return true;
	    } else {
		    if (r1->fed_by.empty()) {
			    if (r2->fed_by.empty()) {
				    /* no ardour-based connections inbound to either route. just use signal order */
				    return r1->order_key(N_("signal")) < r2->order_key(N_("signal"));
			    } else {
				    /* r2 has connections, r1 does not; run r1 early */
				    return true;
			    }
		    } else {
			    return r1->order_key(N_("signal")) < r2->order_key(N_("signal"));
		    }
	    }
    }
};

static void
trace_terminal (shared_ptr<Route> r1, shared_ptr<Route> rbase)
{
	shared_ptr<Route> r2;

	if ((r1->fed_by.find (rbase) != r1->fed_by.end()) && (rbase->fed_by.find (r1) != rbase->fed_by.end())) {
		info << string_compose(_("feedback loop setup between %1 and %2"), r1->name(), rbase->name()) << endmsg;
		return;
	}

	/* make a copy of the existing list of routes that feed r1 */

	set<shared_ptr<Route> > existing = r1->fed_by;

	/* for each route that feeds r1, recurse, marking it as feeding
	   rbase as well.
	*/

	for (set<shared_ptr<Route> >::iterator i = existing.begin(); i != existing.end(); ++i) {
		r2 =* i;

		/* r2 is a route that feeds r1 which somehow feeds base. mark
		   base as being fed by r2
		*/

		rbase->fed_by.insert (r2);

		if (r2 != rbase) {

			/* 2nd level feedback loop detection. if r1 feeds or is fed by r2,
			   stop here.
			 */

			if ((r1->fed_by.find (r2) != r1->fed_by.end()) && (r2->fed_by.find (r1) != r2->fed_by.end())) {
				continue;
			}

			/* now recurse, so that we can mark base as being fed by
			   all routes that feed r2
			*/

			trace_terminal (r2, rbase);
		}

	}
}

void
Session::resort_routes ()
{
	/* don't do anything here with signals emitted
	   by Routes while we are being destroyed.
	*/

	if (_state_of_the_state & Deletion) {
		return;
	}


	{

		RCUWriter<RouteList> writer (routes);
		shared_ptr<RouteList> r = writer.get_copy ();
		resort_routes_using (r);
		/* writer goes out of scope and forces update */
	}

}
void
Session::resort_routes_using (shared_ptr<RouteList> r)
{
	RouteList::iterator i, j;

	for (i = r->begin(); i != r->end(); ++i) {

		(*i)->fed_by.clear ();

		for (j = r->begin(); j != r->end(); ++j) {

			/* although routes can feed themselves, it will
			   cause an endless recursive descent if we
			   detect it. so don't bother checking for
			   self-feeding.
			*/

			if (*j == *i) {
				continue;
			}

			if ((*j)->feeds (*i)) {
				(*i)->fed_by.insert (*j);
			}
		}
	}

	for (i = r->begin(); i != r->end(); ++i) {
		trace_terminal (*i, *i);
	}

	RouteSorter cmp;
	r->sort (cmp);

#if 0
	cerr << "finished route resort\n";

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		cerr << " " << (*i)->name() << " signal order = " << (*i)->order_key ("signal") << endl;
	}
	cerr << endl;
#endif

}

list<boost::shared_ptr<MidiTrack> >
Session::new_midi_track (TrackMode mode, uint32_t how_many)
{
	char track_name[32];
	uint32_t track_id = 0;
	uint32_t n = 0;
	string port;
	RouteList new_routes;
	list<boost::shared_ptr<MidiTrack> > ret;
	//uint32_t control_id;

	// FIXME: need physical I/O and autoconnect stuff for MIDI

	/* count existing midi tracks */

	{
		shared_ptr<RouteList> r = routes.reader ();

		for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
			if (boost::dynamic_pointer_cast<MidiTrack>(*i) != 0) {
				if (!(*i)->is_hidden()) {
					n++;
					//channels_used += (*i)->n_inputs().n_midi();
				}
			}
		}
	}

	vector<string> physinputs;
	vector<string> physoutputs;

	_engine.get_physical_outputs (DataType::MIDI, physoutputs);
	_engine.get_physical_inputs (DataType::MIDI, physinputs);

	// control_id = ntracks() + nbusses();

	while (how_many) {

		/* check for duplicate route names, since we might have pre-existing
		   routes with this name (e.g. create Audio1, Audio2, delete Audio1,
		   save, close,restart,add new route - first named route is now
		   Audio2)
		*/


		do {
			++track_id;

			snprintf (track_name, sizeof(track_name), "Midi %" PRIu32, track_id);

			if (route_by_name (track_name) == 0) {
				break;
			}

		} while (track_id < (UINT_MAX-1));

		shared_ptr<MidiTrack> track;

		try {
			track = boost::shared_ptr<MidiTrack>((new MidiTrack (*this, track_name, Route::Flag (0), mode)));

			if (track->input()->ensure_io (ChanCount(DataType::MIDI, 1), false, this)) {
				error << "cannot configure 1 in/1 out configuration for new midi track" << endmsg;
				goto failed;
			}


			if (track->output()->ensure_io (ChanCount(DataType::AUDIO, 1), false, this)) {
				error << "cannot configure 1 in/1 out configuration for new midi track" << endmsg;
				goto failed;
			}

			/*
			if (nphysical_in) {
				for (uint32_t x = 0; x < track->n_inputs().n_midi() && x < nphysical_in; ++x) {

					port = "";

					if (Config->get_input_auto_connect() & AutoConnectPhysical) {
						port = physinputs[(channels_used+x)%nphysical_in];
					}

					if (port.length() && track->connect_input (track->input (x), port, this)) {
						break;
					}
				}
			}

			for (uint32_t x = 0; x < track->n_outputs().n_midi(); ++x) {

				port = "";

				if (nphysical_out && (Config->get_output_auto_connect() & AutoConnectPhysical)) {
					port = physoutputs[(channels_used+x)%nphysical_out];
				} else if (Config->get_output_auto_connect() & AutoConnectMaster) {
					if (_master_out) {
						port = _master_out->input (x%_master_out->n_inputs().n_midi())->name();
					}
				}

				if (port.length() && track->connect_output (track->output (x), port, this)) {
					break;
				}
			}

			channels_used += track->n_inputs ().n_midi();

			*/

			track->midi_diskstream()->non_realtime_input_change();

			track->DiskstreamChanged.connect (mem_fun (this, &Session::resort_routes));
			//track->set_remote_control_id (control_id);

			new_routes.push_back (track);
			ret.push_back (track);
		}

		catch (failed_constructor &err) {
			error << _("Session: could not create new midi track.") << endmsg;

			if (track) {
				/* we need to get rid of this, since the track failed to be created */
				/* XXX arguably, AudioTrack::AudioTrack should not do the Session::add_diskstream() */

				{
					RCUWriter<DiskstreamList> writer (diskstreams);
					boost::shared_ptr<DiskstreamList> ds = writer.get_copy();
					ds->remove (track->midi_diskstream());
				}
			}

			goto failed;
		}

		catch (AudioEngine::PortRegistrationFailure& pfe) {

			error << _("No more JACK ports are available. You will need to stop Ardour and restart JACK with ports if you need this many tracks.") << endmsg;

			if (track) {
				/* we need to get rid of this, since the track failed to be created */
				/* XXX arguably, MidiTrack::MidiTrack should not do the Session::add_diskstream() */

				{
					RCUWriter<DiskstreamList> writer (diskstreams);
					boost::shared_ptr<DiskstreamList> ds = writer.get_copy();
					ds->remove (track->midi_diskstream());
				}
			}

			goto failed;
		}

		--how_many;
	}

  failed:
	if (!new_routes.empty()) {
		add_routes (new_routes, false);
		save_state (_current_snapshot_name);
	}

	return ret;
}

list<boost::shared_ptr<AudioTrack> >
Session::new_audio_track (int input_channels, int output_channels, TrackMode mode, uint32_t how_many)
{
	char track_name[32];
	uint32_t track_id = 0;
	uint32_t n = 0;
	uint32_t channels_used = 0;
	string port;
	RouteList new_routes;
	list<boost::shared_ptr<AudioTrack> > ret;
	uint32_t control_id;

	/* count existing audio tracks */

	{
		shared_ptr<RouteList> r = routes.reader ();

		for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
			if (boost::dynamic_pointer_cast<AudioTrack>(*i) != 0) {
				if (!(*i)->is_hidden()) {
					n++;
					channels_used += (*i)->n_inputs().n_audio();
				}
			}
		}
	}

	vector<string> physinputs;
	vector<string> physoutputs;

	_engine.get_physical_outputs (DataType::AUDIO, physoutputs);
	_engine.get_physical_inputs (DataType::AUDIO, physinputs);

	control_id = ntracks() + nbusses() + 1;

	while (how_many) {

		/* check for duplicate route names, since we might have pre-existing
		   routes with this name (e.g. create Audio1, Audio2, delete Audio1,
		   save, close,restart,add new route - first named route is now
		   Audio2)
		*/


		do {
			++track_id;

			snprintf (track_name, sizeof(track_name), "Audio %" PRIu32, track_id);

			if (route_by_name (track_name) == 0) {
				break;
			}

		} while (track_id < (UINT_MAX-1));

		shared_ptr<AudioTrack> track;

		try {
			track = boost::shared_ptr<AudioTrack>((new AudioTrack (*this, track_name, Route::Flag (0), mode)));

			if (track->input()->ensure_io (ChanCount(DataType::AUDIO, input_channels), false, this)) {
				error << string_compose (_("cannot configure %1 in/%2 out configuration for new audio track"),
							 input_channels, output_channels)
				      << endmsg;
				goto failed;
			}
			
			if (track->output()->ensure_io (ChanCount(DataType::AUDIO, output_channels), false, this)) {
				error << string_compose (_("cannot configure %1 in/%2 out configuration for new audio track"),
							 input_channels, output_channels)
				      << endmsg;
				goto failed;
			}

			if (!physinputs.empty()) {
				uint32_t nphysical_in = physinputs.size();

				for (uint32_t x = 0; x < track->n_inputs().n_audio() && x < nphysical_in; ++x) {

					port = "";

					if (Config->get_input_auto_connect() & AutoConnectPhysical) {
						port = physinputs[(channels_used+x)%nphysical_in];
					}

					if (port.length() && track->input()->connect (track->input()->nth(x), port, this)) {
						break;
					}
				}
			}

			if (!physoutputs.empty()) {
				uint32_t nphysical_out = physoutputs.size();

				for (uint32_t x = 0; x < track->n_outputs().n_audio(); ++x) {
					port = "";
					
					if (Config->get_output_auto_connect() & AutoConnectPhysical) {
						port = physoutputs[(channels_used+x)%nphysical_out];
					} else if (Config->get_output_auto_connect() & AutoConnectMaster) {
						if (_master_out && _master_out->n_inputs().n_audio() > 0) {
							port = _master_out->input()->nth (x % _master_out->input()->n_ports().n_audio())->name();
						}
					}
					
					if (port.length() && track->output()->connect (track->output()->nth(x), port, this)) {
						break;
					}
				}
			}

			channels_used += track->n_inputs ().n_audio();

			track->audio_diskstream()->non_realtime_input_change();

			track->DiskstreamChanged.connect (mem_fun (this, &Session::resort_routes));
			track->set_remote_control_id (control_id);
			++control_id;

			new_routes.push_back (track);
			ret.push_back (track);
		}

		catch (failed_constructor &err) {
			error << _("Session: could not create new audio track.") << endmsg;

			if (track) {
				/* we need to get rid of this, since the track failed to be created */
				/* XXX arguably, AudioTrack::AudioTrack should not do the Session::add_diskstream() */

				{
					RCUWriter<DiskstreamList> writer (diskstreams);
					boost::shared_ptr<DiskstreamList> ds = writer.get_copy();
					ds->remove (track->audio_diskstream());
				}
			}

			goto failed;
		}

		catch (AudioEngine::PortRegistrationFailure& pfe) {

			error << pfe.what() << endmsg;

			if (track) {
				/* we need to get rid of this, since the track failed to be created */
				/* XXX arguably, AudioTrack::AudioTrack should not do the Session::add_diskstream() */

				{
					RCUWriter<DiskstreamList> writer (diskstreams);
					boost::shared_ptr<DiskstreamList> ds = writer.get_copy();
					ds->remove (track->audio_diskstream());
				}
			}

			goto failed;
		}

		--how_many;
	}

  failed:
	if (!new_routes.empty()) {
		add_routes (new_routes, true);
	}

	return ret;
}

void
Session::set_remote_control_ids ()
{
	RemoteModel m = Config->get_remote_model();

	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if ( MixerOrdered == m) {
			long order = (*i)->order_key(N_("signal"));
			(*i)->set_remote_control_id( order+1 );
		} else if ( EditorOrdered == m) {
			long order = (*i)->order_key(N_("editor"));
			(*i)->set_remote_control_id( order+1 );
		} else if ( UserOrdered == m) {
			//do nothing ... only changes to remote id's are initiated by user
		}
	}
}


RouteList
Session::new_audio_route (int input_channels, int output_channels, uint32_t how_many)
{
	char bus_name[32];
	uint32_t bus_id = 1;
	uint32_t n = 0;
	uint32_t channels_used = 0;
	string port;
	RouteList ret;
	uint32_t control_id;

	/* count existing audio busses */

	{
		shared_ptr<RouteList> r = routes.reader ();

		for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
			if (boost::dynamic_pointer_cast<Track>(*i) == 0) {
				/* its a bus ? */
				if (!(*i)->is_hidden() && (*i)->name() != _("master")) {
					bus_id++;
					n++;
					channels_used += (*i)->n_inputs().n_audio();
				}
			}
		}
	}

	vector<string> physinputs;
	vector<string> physoutputs;

	_engine.get_physical_outputs (DataType::AUDIO, physoutputs);
	_engine.get_physical_inputs (DataType::AUDIO, physinputs);

	n_physical_audio_outputs = physoutputs.size();
	n_physical_audio_inputs = physinputs.size();

	control_id = ntracks() + nbusses() + 1;

	while (how_many) {

		do {
			snprintf (bus_name, sizeof(bus_name), "Bus %" PRIu32, bus_id);

			bus_id++;

			if (route_by_name (bus_name) == 0) {
				break;
			}

		} while (bus_id < (UINT_MAX-1));

		try {
			shared_ptr<Route> bus (new Route (*this, bus_name, Route::Flag(0), DataType::AUDIO));

			if (bus->input()->ensure_io (ChanCount(DataType::AUDIO, input_channels), false, this)) {
				error << string_compose (_("cannot configure %1 in/%2 out configuration for new audio track"),
							 input_channels, output_channels)
				      << endmsg;
				goto failure;
			}


			if (bus->output()->ensure_io (ChanCount(DataType::AUDIO, output_channels), false, this)) {
				error << string_compose (_("cannot configure %1 in/%2 out configuration for new audio track"),
							 input_channels, output_channels)
				      << endmsg;
				goto failure;
			}

			for (uint32_t x = 0; n_physical_audio_inputs && x < bus->input()->n_ports().n_audio(); ++x) {
				port = "";
				
				if (Config->get_input_auto_connect() & AutoConnectPhysical) {
					port = physinputs[((n+x)%n_physical_audio_inputs)];
				} 
				
				if (port.length() && bus->input()->connect (bus->input()->nth (x), port, this)) {
					break;
				}
			}

			for (uint32_t x = 0; n_physical_audio_outputs && x < bus->n_outputs().n_audio(); ++x) {
				port = "";

				if (Config->get_output_auto_connect() & AutoConnectPhysical) {
					port = physoutputs[((n+x)%n_physical_outputs)];
				} else if (Config->get_output_auto_connect() & AutoConnectMaster) {
					if (_master_out) {
						port = _master_out->input()->nth (x%_master_out->input()->n_ports().n_audio())->name();
					}
				}

				if (port.length() && bus->output()->connect (bus->output()->nth(x), port, this)) {
					break;
				}
			}

			channels_used += bus->n_inputs ().n_audio();

			bus->set_remote_control_id (control_id);
			++control_id;

			ret.push_back (bus);
		}


		catch (failed_constructor &err) {
			error << _("Session: could not create new audio route.") << endmsg;
			goto failure;
		}

		catch (AudioEngine::PortRegistrationFailure& pfe) {
			error << pfe.what() << endmsg;
			goto failure;
		}


		--how_many;
	}

  failure:
	if (!ret.empty()) {
		add_routes (ret, true);
	}

	return ret;

}

RouteList
Session::new_route_from_template (uint32_t how_many, const std::string& template_path)
{
	char name[32];
	RouteList ret;
	uint32_t control_id;
	XMLTree tree;

	if (!tree.read (template_path.c_str())) {
		return ret;
	}

	XMLNode* node = tree.root();

	control_id = ntracks() + nbusses() + 1;

	while (how_many) {

		XMLNode node_copy (*node); // make a copy so we can change the name if we need to
	  
		std::string node_name = IO::name_from_state (*node_copy.children().front());

		if (route_by_name (node_name) != 0) {

			/* generate a new name by adding a number to the end of the template name */

			uint32_t number = 1;

			do {
				snprintf (name, sizeof (name), "%s %" PRIu32, node_name.c_str(), number);
	      
				number++;
	      
				if (route_by_name (name) == 0) {
					break;
				}
	      
			} while (number < UINT_MAX);

			if (number == UINT_MAX) {
				fatal << _("Session: UINT_MAX routes? impossible!") << endmsg;
				/*NOTREACHED*/
			}

			IO::set_name_in_state (*node_copy.children().front(), name);
		}

		Track::zero_diskstream_id_in_xml (node_copy);

		try {
			shared_ptr<Route> route (XMLRouteFactory (node_copy));
	    
			if (route == 0) {
				error << _("Session: cannot create track/bus from template description") << endmsg;
				goto out;
			}

			if (boost::dynamic_pointer_cast<Track>(route)) {
				/* force input/output change signals so that the new diskstream
				   picks up the configuration of the route. During session
				   loading this normally happens in a different way.
				*/
				route->input()->changed (IOChange (ConfigurationChanged|ConnectionsChanged), this);
				route->output()->changed (IOChange (ConfigurationChanged|ConnectionsChanged), this);
			}
			
			route->set_remote_control_id (control_id);
			++control_id;
	    
			ret.push_back (route);
		}
	  
		catch (failed_constructor &err) {
			error << _("Session: could not create new route from template") << endmsg;
			goto out;
		}
	  
		catch (AudioEngine::PortRegistrationFailure& pfe) {
			error << pfe.what() << endmsg;
			goto out;
		}
	  
		--how_many;
	}

  out:
	if (!ret.empty()) {
		add_routes (ret, true);
	}

	return ret;
}

void
Session::add_routes (RouteList& new_routes, bool save)
{
	{
		RCUWriter<RouteList> writer (routes);
		shared_ptr<RouteList> r = writer.get_copy ();
		r->insert (r->end(), new_routes.begin(), new_routes.end());

		if (!_control_out && IO::connecting_legal) {
			resort_routes_using (r);
		}
	}

	for (RouteList::iterator x = new_routes.begin(); x != new_routes.end(); ++x) {

		boost::weak_ptr<Route> wpr (*x);

		(*x)->solo_changed.connect (sigc::bind (mem_fun (*this, &Session::route_solo_changed), wpr));
		(*x)->mute_changed.connect (mem_fun (*this, &Session::route_mute_changed));
		(*x)->output()->changed.connect (mem_fun (*this, &Session::set_worst_io_latencies_x));
		(*x)->processors_changed.connect (bind (mem_fun (*this, &Session::update_latency_compensation), false, false));

		if ((*x)->is_master()) {
			_master_out = (*x);
		}

		if ((*x)->is_control()) {
			_control_out = (*x);
		}
	}

	if (_control_out && IO::connecting_legal) {

		for (RouteList::iterator x = new_routes.begin(); x != new_routes.end(); ++x) {
			(*x)->listen_via (_control_out, "control");
		}

		resort_routes ();
	}

	set_dirty();

	if (save) {
		save_state (_current_snapshot_name);
	}

	RouteAdded (new_routes); /* EMIT SIGNAL */
}

void
Session::add_diskstream (boost::shared_ptr<Diskstream> dstream)
{
	/* need to do this in case we're rolling at the time, to prevent false underruns */
	dstream->do_refill_with_alloc ();

	dstream->set_block_size (current_block_size);

	{
		RCUWriter<DiskstreamList> writer (diskstreams);
		boost::shared_ptr<DiskstreamList> ds = writer.get_copy();
		ds->push_back (dstream);
		/* writer goes out of scope, copies ds back to main */
	}

	dstream->PlaylistChanged.connect (sigc::bind (mem_fun (*this, &Session::diskstream_playlist_changed), dstream));
	/* this will connect to future changes, and check the current length */
	diskstream_playlist_changed (dstream);

	dstream->RecordEnableChanged.connect (mem_fun (*this, &Session::update_have_rec_enabled_diskstream));

	dstream->prepare ();

}

void
Session::remove_route (shared_ptr<Route> route)
{
	{
		RCUWriter<RouteList> writer (routes);
		shared_ptr<RouteList> rs = writer.get_copy ();

		rs->remove (route);

		/* deleting the master out seems like a dumb
		   idea, but its more of a UI policy issue
		   than our concern.
		*/

		if (route == _master_out) {
			_master_out = shared_ptr<Route> ();
		}

		if (route == _control_out) {

			/* cancel control outs for all routes */

			for (RouteList::iterator r = rs->begin(); r != rs->end(); ++r) {
				(*r)->drop_listen (_control_out);
			}

			_control_out.reset ();
		}

		update_route_solo_state ();

		/* writer goes out of scope, forces route list update */
	}

	boost::shared_ptr<Track> t;
	boost::shared_ptr<Diskstream> ds;

	if ((t = boost::dynamic_pointer_cast<Track>(route)) != 0) {
		ds = t->diskstream();
	}

	if (ds) {

		{
			RCUWriter<DiskstreamList> dsl (diskstreams);
			boost::shared_ptr<DiskstreamList> d = dsl.get_copy();
			d->remove (ds);
		}
	}

	find_current_end ();

	// We need to disconnect the routes inputs and outputs

	route->input()->disconnect (0);
	route->output()->disconnect (0);

	update_latency_compensation (false, false);
	set_dirty();

	/* get rid of it from the dead wood collection in the route list manager */

	/* XXX i think this is unsafe as it currently stands, but i am not sure. (pd, october 2nd, 2006) */

	routes.flush ();

	/* try to cause everyone to drop their references */

	route->drop_references ();

	sync_order_keys (N_("session"));

	/* save the new state of the world */

	if (save_state (_current_snapshot_name)) {
		save_history (_current_snapshot_name);
	}
}

void
Session::route_mute_changed (void* src)
{
	set_dirty ();
}

void
Session::route_solo_changed (void* src, boost::weak_ptr<Route> wpr)
{
	if (solo_update_disabled) {
		// We know already
		return;
	}

	boost::shared_ptr<Route> route = wpr.lock ();

	if (!route) {
		/* should not happen */
		error << string_compose (_("programming error: %1"), X_("invalid route weak ptr passed to route_solo_changed")) << endmsg;
		return;
	}

	shared_ptr<RouteList> r = routes.reader ();
	int32_t delta;

	if (route->soloed()) {
		delta = 1;
	} else {
		delta = -1;
	}

	solo_update_disabled = true;
	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {

		if ((*i)->feeds (route)) {
			/* do it */
			
			(*i)->main_outs()->mod_solo_level (delta);
		}
	}

	/* make sure master is never muted by solo */

	if (_master_out->main_outs()->solo_level() == 0) {
		_master_out->main_outs()->mod_solo_level (1);
	}

	solo_update_disabled = false;
	update_route_solo_state (r);
	SoloChanged (); /* EMIT SIGNAL */
	set_dirty();
}

void
Session::update_route_solo_state (boost::shared_ptr<RouteList> r)
{
	/* now figure out if anything that matters is soloed */

	bool something_soloed = false;

	if (!r) {
		r = routes.reader();
	}

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if (!(*i)->is_master() && !(*i)->is_hidden() && (*i)->soloed()) {
			something_soloed = true;
			break;
		}
	}

	if (something_soloed != _non_soloed_outs_muted) {
		_non_soloed_outs_muted = something_soloed;
		SoloActive (_non_soloed_outs_muted); /* EMIT SIGNAL */
	}
}

void
Session::catch_up_on_solo ()
{
	/* this is called after set_state() to catch the full solo
	   state, which can't be correctly determined on a per-route
	   basis, but needs the global overview that only the session
	   has.
	*/
	update_route_solo_state();
}	

void
Session::catch_up_on_solo_mute_override ()
{
	if (Config->get_solo_model() != InverseMute) {
		return;
	}

	/* this is called whenever the param solo-mute-override is
	   changed.
	*/
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		// (*i)->catch_up_on_solo_mute_override ();
	}
}	

shared_ptr<Route>
Session::route_by_name (string name)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if ((*i)->name() == name) {
			return *i;
		}
	}

	return shared_ptr<Route> ((Route*) 0);
}

shared_ptr<Route>
Session::route_by_id (PBD::ID id)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if ((*i)->id() == id) {
			return *i;
		}
	}

	return shared_ptr<Route> ((Route*) 0);
}

shared_ptr<Route>
Session::route_by_remote_id (uint32_t id)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if ((*i)->remote_control_id() == id) {
			return *i;
		}
	}

	return shared_ptr<Route> ((Route*) 0);
}

void
Session::find_current_end ()
{
	if (_state_of_the_state & Loading) {
		return;
	}

	nframes_t max = get_maximum_extent ();

	if (max > end_location->end()) {
		end_location->set_end (max);
		set_dirty();
		DurationChanged(); /* EMIT SIGNAL */
	}
}

nframes_t
Session::get_maximum_extent () const
{
	nframes_t max = 0;
	nframes_t me;

	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::const_iterator i = dsl->begin(); i != dsl->end(); ++i) {
		if ((*i)->destructive())  //ignore tape tracks when getting max extents
			continue;
		boost::shared_ptr<Playlist> pl = (*i)->playlist();
		if ((me = pl->get_maximum_extent()) > max) {
			max = me;
		}
	}

	return max;
}

boost::shared_ptr<Diskstream>
Session::diskstream_by_name (string name)
{
	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		if ((*i)->name() == name) {
			return *i;
		}
	}

	return boost::shared_ptr<Diskstream>((Diskstream*) 0);
}

boost::shared_ptr<Diskstream>
Session::diskstream_by_id (const PBD::ID& id)
{
	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		if ((*i)->id() == id) {
			return *i;
		}
	}

	return boost::shared_ptr<Diskstream>((Diskstream*) 0);
}

/* Region management */

string
Session::new_region_name (string old)
{
	string::size_type last_period;
	uint32_t number;
	string::size_type len = old.length() + 64;
	char buf[len];

	if ((last_period = old.find_last_of ('.')) == string::npos) {

		/* no period present - add one explicitly */

		old += '.';
		last_period = old.length() - 1;
		number = 0;

	} else {

		number = atoi (old.substr (last_period+1).c_str());

	}

	while (number < (UINT_MAX-1)) {

		RegionList::const_iterator i;
		string sbuf;

		number++;

		snprintf (buf, len, "%s%" PRIu32, old.substr (0, last_period + 1).c_str(), number);
		sbuf = buf;

		for (i = regions.begin(); i != regions.end(); ++i) {
			if (i->second->name() == sbuf) {
				break;
			}
		}

		if (i == regions.end()) {
			break;
		}
	}

	if (number != (UINT_MAX-1)) {
		return buf;
	}

	error << string_compose (_("cannot create new name for region \"%1\""), old) << endmsg;
	return old;
}

int
Session::region_name (string& result, string base, bool newlevel)
{
	char buf[16];
	string subbase;

	if (base.find("/") != string::npos) {
		base = base.substr(base.find_last_of("/") + 1);
	}

	if (base == "") {

		Glib::Mutex::Lock lm (region_lock);

		snprintf (buf, sizeof (buf), "%d", (int)regions.size() + 1);
		result = "region.";
		result += buf;

	} else {

		if (newlevel) {
			subbase = base;
		} else {
			string::size_type pos;

			pos = base.find_last_of ('.');

			/* pos may be npos, but then we just use entire base */

			subbase = base.substr (0, pos);

		}

		{
			Glib::Mutex::Lock lm (region_lock);

			map<string,uint32_t>::iterator x;

			result = subbase;

			if ((x = region_name_map.find (subbase)) == region_name_map.end()) {
				result += ".1";
				region_name_map[subbase] = 1;
			} else {
				x->second++;
				snprintf (buf, sizeof (buf), ".%d", x->second);

				result += buf;
			}
		}
	}

	return 0;
}

void
Session::add_region (boost::shared_ptr<Region> region)
{
	vector<boost::shared_ptr<Region> > v;
	v.push_back (region);
	add_regions (v);
}

void
Session::add_regions (vector<boost::shared_ptr<Region> >& new_regions)
{
	bool added = false;

	{
		Glib::Mutex::Lock lm (region_lock);

		for (vector<boost::shared_ptr<Region> >::iterator ii = new_regions.begin(); ii != new_regions.end(); ++ii) {

			boost::shared_ptr<Region> region = *ii;

			if (region == 0) {

				error << _("Session::add_region() ignored a null region. Warning: you might have lost a region.") << endmsg;

			} else {

				RegionList::iterator x;

				for (x = regions.begin(); x != regions.end(); ++x) {

					if (region->region_list_equivalent (x->second)) {
						break;
					}
				}

				if (x == regions.end()) {

					pair<RegionList::key_type,RegionList::mapped_type> entry;

					entry.first = region->id();
					entry.second = region;

					pair<RegionList::iterator,bool> x = regions.insert (entry);

					if (!x.second) {
						return;
					}

					added = true;
				}
			}
		}
	}

	/* mark dirty because something has changed even if we didn't
	   add the region to the region list.
	*/

	set_dirty ();

	if (added) {

		vector<boost::weak_ptr<Region> > v;
		boost::shared_ptr<Region> first_r;

		for (vector<boost::shared_ptr<Region> >::iterator ii = new_regions.begin(); ii != new_regions.end(); ++ii) {

			boost::shared_ptr<Region> region = *ii;

			if (region == 0) {

				error << _("Session::add_region() ignored a null region. Warning: you might have lost a region.") << endmsg;

			} else {
				v.push_back (region);

				if (!first_r) {
					first_r = region;
				}
			}

			region->StateChanged.connect (sigc::bind (mem_fun (*this, &Session::region_changed), boost::weak_ptr<Region>(region)));
			region->GoingAway.connect (sigc::bind (mem_fun (*this, &Session::remove_region), boost::weak_ptr<Region>(region)));

			update_region_name_map (region);
		}

		if (!v.empty()) {
			RegionsAdded (v); /* EMIT SIGNAL */
		}
	}
}

void
Session::update_region_name_map (boost::shared_ptr<Region> region)
{
	string::size_type last_period = region->name().find_last_of ('.');
	
	if (last_period != string::npos && last_period < region->name().length() - 1) {
		
		string base = region->name().substr (0, last_period);
		string number = region->name().substr (last_period+1);
		map<string,uint32_t>::iterator x;
		
		/* note that if there is no number, we get zero from atoi,
		   which is just fine
		*/
		
		region_name_map[base] = atoi (number);
	}
}

void
Session::region_changed (Change what_changed, boost::weak_ptr<Region> weak_region)
{
	boost::shared_ptr<Region> region (weak_region.lock ());

	if (!region) {
		return;
	}

	if (what_changed & Region::HiddenChanged) {
		/* relay hidden changes */
		RegionHiddenChange (region);
	}

	if (what_changed & NameChanged) {
		update_region_name_map (region);
	}
}

void
Session::remove_region (boost::weak_ptr<Region> weak_region)
{
	RegionList::iterator i;
	boost::shared_ptr<Region> region (weak_region.lock ());

	if (!region) {
		return;
	}

	bool removed = false;

	{
		Glib::Mutex::Lock lm (region_lock);

		if ((i = regions.find (region->id())) != regions.end()) {
			regions.erase (i);
			removed = true;
		}
	}

	/* mark dirty because something has changed even if we didn't
	   remove the region from the region list.
	*/

	set_dirty();

	if (removed) {
		 RegionRemoved(region); /* EMIT SIGNAL */
	}
}

boost::shared_ptr<Region>
Session::find_whole_file_parent (boost::shared_ptr<Region const> child)
{
	RegionList::iterator i;
	boost::shared_ptr<Region> region;

	Glib::Mutex::Lock lm (region_lock);

	for (i = regions.begin(); i != regions.end(); ++i) {

		region = i->second;

		if (region->whole_file()) {

			if (child->source_equivalent (region)) {
				return region;
			}
		}
	}

	return boost::shared_ptr<Region> ();
}

void
Session::find_equivalent_playlist_regions (boost::shared_ptr<Region> region, vector<boost::shared_ptr<Region> >& result)
{
	for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ++i)
		(*i)->get_region_list_equivalent_regions (region, result);
}

int
Session::destroy_region (boost::shared_ptr<Region> region)
{
	vector<boost::shared_ptr<Source> > srcs;

	{
		if (region->playlist()) {
			region->playlist()->destroy_region (region);
		}

		for (uint32_t n = 0; n < region->n_channels(); ++n) {
			srcs.push_back (region->source (n));
		}
	}

	region->drop_references ();

	for (vector<boost::shared_ptr<Source> >::iterator i = srcs.begin(); i != srcs.end(); ++i) {

			(*i)->mark_for_remove ();
			(*i)->drop_references ();

			cerr << "source was not used by any playlist\n";
	}

	return 0;
}

int
Session::destroy_regions (list<boost::shared_ptr<Region> > regions)
{
	for (list<boost::shared_ptr<Region> >::iterator i = regions.begin(); i != regions.end(); ++i) {
		destroy_region (*i);
	}
	return 0;
}

int
Session::remove_last_capture ()
{
	list<boost::shared_ptr<Region> > r;

	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		list<boost::shared_ptr<Region> >& l = (*i)->last_capture_regions();

		if (!l.empty()) {
			r.insert (r.end(), l.begin(), l.end());
			l.clear ();
		}
	}

	destroy_regions (r);

	save_state (_current_snapshot_name);

	return 0;
}

int
Session::remove_region_from_region_list (boost::shared_ptr<Region> r)
{
	remove_region (r);
	return 0;
}

/* Source Management */

void
Session::add_source (boost::shared_ptr<Source> source)
{
	pair<SourceMap::key_type, SourceMap::mapped_type> entry;
	pair<SourceMap::iterator,bool> result;

	entry.first = source->id();
	entry.second = source;

	{
		Glib::Mutex::Lock lm (source_lock);
		result = sources.insert (entry);
	}

	if (result.second) {
		source->GoingAway.connect (sigc::bind (mem_fun (this, &Session::remove_source), boost::weak_ptr<Source> (source)));
		set_dirty();
	}

	boost::shared_ptr<AudioFileSource> afs;

	if ((afs = boost::dynamic_pointer_cast<AudioFileSource>(source)) != 0) {
		if (Config->get_auto_analyse_audio()) {
			Analyser::queue_source_for_analysis (source, false);
		}
	}
}

void
Session::remove_source (boost::weak_ptr<Source> src)
{
	SourceMap::iterator i;
	boost::shared_ptr<Source> source = src.lock();

	if (!source) {
		return;
	}

	{
		Glib::Mutex::Lock lm (source_lock);

		if ((i = sources.find (source->id())) != sources.end()) {
			sources.erase (i);
		}
	}

	if (!_state_of_the_state & InCleanup) {

		/* save state so we don't end up with a session file
		   referring to non-existent sources.
		*/

		save_state (_current_snapshot_name);
	}
}

boost::shared_ptr<Source>
Session::source_by_id (const PBD::ID& id)
{
	Glib::Mutex::Lock lm (source_lock);
	SourceMap::iterator i;
	boost::shared_ptr<Source> source;

	if ((i = sources.find (id)) != sources.end()) {
		source = i->second;
	}

	return source;
}

boost::shared_ptr<Source>
Session::source_by_path_and_channel (const Glib::ustring& path, uint16_t chn)
{
	Glib::Mutex::Lock lm (source_lock);

	for (SourceMap::iterator i = sources.begin(); i != sources.end(); ++i) {
		cerr << "comparing " << path << " with " << i->second->name() << endl;
		boost::shared_ptr<AudioFileSource> afs
			= boost::dynamic_pointer_cast<AudioFileSource>(i->second);

		if (afs && afs->path() == path && chn == afs->channel()) {
			return afs;
		}
	}
	return boost::shared_ptr<Source>();
}


string
Session::change_source_path_by_name (string path, string oldname, string newname, bool destructive)
{
	string look_for;
	string old_basename = PBD::basename_nosuffix (oldname);
	string new_legalized = legalize_for_path (newname);

	/* note: we know (or assume) the old path is already valid */

	if (destructive) {

		/* destructive file sources have a name of the form:

		    /path/to/Tnnnn-NAME(%[LR])?.wav

		    the task here is to replace NAME with the new name.
		*/

		/* find last slash */

		string dir;
		string prefix;
		string::size_type slash;
		string::size_type dash;

		if ((slash = path.find_last_of ('/')) == string::npos) {
			return "";
		}

		dir = path.substr (0, slash+1);

		/* '-' is not a legal character for the NAME part of the path */

		if ((dash = path.find_last_of ('-')) == string::npos) {
			return "";
		}

		prefix = path.substr (slash+1, dash-(slash+1));

		path = dir;
		path += prefix;
		path += '-';
		path += new_legalized;
		path += ".wav";  /* XXX gag me with a spoon */

	} else {

		/* non-destructive file sources have a name of the form:

		    /path/to/NAME-nnnnn(%[LR])?.ext

		    the task here is to replace NAME with the new name.
		*/

		string dir;
		string suffix;
		string::size_type slash;
		string::size_type dash;
		string::size_type postfix;

		/* find last slash */

		if ((slash = path.find_last_of ('/')) == string::npos) {
			return "";
		}

		dir = path.substr (0, slash+1);

		/* '-' is not a legal character for the NAME part of the path */

		if ((dash = path.find_last_of ('-')) == string::npos) {
			return "";
		}

		suffix = path.substr (dash+1);

		// Suffix is now everything after the dash. Now we need to eliminate
		// the nnnnn part, which is done by either finding a '%' or a '.'

		postfix = suffix.find_last_of ("%");
		if (postfix == string::npos) {
			postfix = suffix.find_last_of ('.');
		}

		if (postfix != string::npos) {
			suffix = suffix.substr (postfix);
		} else {
			error << "Logic error in Session::change_source_path_by_name(), please report" << endl;
			return "";
		}

		const uint32_t limit = 10000;
		char buf[PATH_MAX+1];

		for (uint32_t cnt = 1; cnt <= limit; ++cnt) {

			snprintf (buf, sizeof(buf), "%s%s-%u%s", dir.c_str(), newname.c_str(), cnt, suffix.c_str());

			if (access (buf, F_OK) != 0) {
				path = buf;
				break;
			}
			path = "";
		}

		if (path == "") {
			error << "FATAL ERROR! Could not find a " << endl;
		}

	}

	return path;
}

/** Return the full path (in some session directory) for a new embedded source.
 * \a name must be a session-unique name that does not contain slashes
 *         (e.g. as returned by new_*_source_name)
 */
string
Session::new_source_path_from_name (DataType type, const string& name)
{
	assert(name.find("/") == string::npos);

	SessionDirectory sdir(get_best_session_directory_for_new_source());

	sys::path p;
	if (type == DataType::AUDIO) {
		p = sdir.sound_path();
	} else if (type == DataType::MIDI) {
		p = sdir.midi_path();
	} else {
		error << "Unknown source type, unable to create file path" << endmsg;
		return "";
	}

	p /= name;
	return p.to_string();
}

Glib::ustring
Session::peak_path (Glib::ustring base) const
{
	sys::path peakfile_path(_session_dir->peak_path());
	peakfile_path /= basename_nosuffix (base) + peakfile_suffix;
	return peakfile_path.to_string();
}

/** Return a unique name based on \a base for a new internal audio source */
string
Session::new_audio_source_name (const string& base, uint32_t nchan, uint32_t chan, bool destructive)
{
	string spath;
	uint32_t cnt;
	char buf[PATH_MAX+1];
	const uint32_t limit = 10000;
	string legalized;

	buf[0] = '\0';
	legalized = legalize_for_path (base);

	// Find a "version" of the base name that doesn't exist in any of the possible directories.
	for (cnt = (destructive ? ++destructive_index : 1); cnt <= limit; ++cnt) {

		vector<space_and_path>::iterator i;
		uint32_t existing = 0;

		for (i = session_dirs.begin(); i != session_dirs.end(); ++i) {

			SessionDirectory sdir((*i).path);

			spath = sdir.sound_path().to_string();

			if (destructive) {

				if (nchan < 2) {
					snprintf (buf, sizeof(buf), "%s/T%04d-%s.wav",
							spath.c_str(), cnt, legalized.c_str());
				} else if (nchan == 2) {
					if (chan == 0) {
						snprintf (buf, sizeof(buf), "%s/T%04d-%s%%L.wav",
								spath.c_str(), cnt, legalized.c_str());
					} else {
						snprintf (buf, sizeof(buf), "%s/T%04d-%s%%R.wav",
								spath.c_str(), cnt, legalized.c_str());
					}
				} else if (nchan < 26) {
					snprintf (buf, sizeof(buf), "%s/T%04d-%s%%%c.wav",
							spath.c_str(), cnt, legalized.c_str(), 'a' + chan);
				} else {
					snprintf (buf, sizeof(buf), "%s/T%04d-%s.wav",
							spath.c_str(), cnt, legalized.c_str());
				}

			} else {

				spath += '/';
				spath += legalized;

				if (nchan < 2) {
					snprintf (buf, sizeof(buf), "%s-%u.wav", spath.c_str(), cnt);
				} else if (nchan == 2) {
					if (chan == 0) {
						snprintf (buf, sizeof(buf), "%s-%u%%L.wav", spath.c_str(), cnt);
					} else {
						snprintf (buf, sizeof(buf), "%s-%u%%R.wav", spath.c_str(), cnt);
					}
				} else if (nchan < 26) {
					snprintf (buf, sizeof(buf), "%s-%u%%%c.wav", spath.c_str(), cnt, 'a' + chan);
				} else {
					snprintf (buf, sizeof(buf), "%s-%u.wav", spath.c_str(), cnt);
				}
			}

			if (sys::exists(buf)) {
				existing++;
			}

		}

		if (existing == 0) {
			break;
		}

		if (cnt > limit) {
			error << string_compose(
					_("There are already %1 recordings for %2, which I consider too many."),
					limit, base) << endmsg;
			destroy ();
			throw failed_constructor();
		}
	}

	return Glib::path_get_basename(buf);
}

/** Create a new embedded audio source */
boost::shared_ptr<AudioFileSource>
Session::create_audio_source_for_session (AudioDiskstream& ds, uint32_t chan, bool destructive)
{
	const size_t n_chans = ds.n_channels().n_audio();
	const string name    = new_audio_source_name (ds.name(), n_chans, chan, destructive);
	const string path    = new_source_path_from_name(DataType::AUDIO, name);
	return boost::dynamic_pointer_cast<AudioFileSource> (
			SourceFactory::createWritable (
					DataType::AUDIO, *this, path, true, destructive, frame_rate()));
}

/** Return a unique name based on \a base for a new internal MIDI source */
string
Session::new_midi_source_name (const string& base)
{
	uint32_t cnt;
	char buf[PATH_MAX+1];
	const uint32_t limit = 10000;
	string legalized;

	buf[0] = '\0';
	legalized = legalize_for_path (base);

	// Find a "version" of the file name that doesn't exist in any of the possible directories.
	for (cnt = 1; cnt <= limit; ++cnt) {

		vector<space_and_path>::iterator i;
		uint32_t existing = 0;

		for (i = session_dirs.begin(); i != session_dirs.end(); ++i) {

			SessionDirectory sdir((*i).path);

			sys::path p = sdir.midi_path();
			p /= legalized;

			snprintf (buf, sizeof(buf), "%s-%u.mid", p.to_string().c_str(), cnt);

			if (sys::exists (buf)) {
				existing++;
			}
		}

		if (existing == 0) {
			break;
		}

		if (cnt > limit) {
			error << string_compose(
					_("There are already %1 recordings for %2, which I consider too many."),
					limit, base) << endmsg;
			destroy ();
			throw failed_constructor();
		}
	}

	return Glib::path_get_basename(buf);
}


/** Create a new embedded MIDI source */
boost::shared_ptr<MidiSource>
Session::create_midi_source_for_session (MidiDiskstream& ds)
{
	const string name = new_midi_source_name (ds.name());
	const string path = new_source_path_from_name (DataType::MIDI, name);

	return boost::dynamic_pointer_cast<SMFSource> (
			SourceFactory::createWritable (
					DataType::MIDI, *this, path, true, false, frame_rate()));
}


/* Playlist management */

boost::shared_ptr<Playlist>
Session::playlist_by_name (string name)
{
	Glib::Mutex::Lock lm (playlist_lock);
	for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if ((*i)->name() == name) {
			return* i;
		}
	}
	for (PlaylistList::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if ((*i)->name() == name) {
			return* i;
		}
	}

	return boost::shared_ptr<Playlist>();
}

void
Session::unassigned_playlists (std::list<boost::shared_ptr<Playlist> > & list)
{
	Glib::Mutex::Lock lm (playlist_lock);
	for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		if (!(*i)->get_orig_diskstream_id().to_s().compare ("0")) {
			list.push_back (*i);
		}
	}
	for (PlaylistList::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		if (!(*i)->get_orig_diskstream_id().to_s().compare ("0")) {
			list.push_back (*i);
		}
	}
}

void
Session::add_playlist (boost::shared_ptr<Playlist> playlist, bool unused)
{
	if (playlist->hidden()) {
		return;
	}

	{
		Glib::Mutex::Lock lm (playlist_lock);
		if (find (playlists.begin(), playlists.end(), playlist) == playlists.end()) {
			playlists.insert (playlists.begin(), playlist);
			playlist->InUse.connect (sigc::bind (mem_fun (*this, &Session::track_playlist), boost::weak_ptr<Playlist>(playlist)));
			playlist->GoingAway.connect (sigc::bind (mem_fun (*this, &Session::remove_playlist), boost::weak_ptr<Playlist>(playlist)));
		}
	}

	if (unused) {
		playlist->release();
	}

	set_dirty();

	PlaylistAdded (playlist); /* EMIT SIGNAL */
}

void
Session::get_playlists (vector<boost::shared_ptr<Playlist> >& s)
{
	{
		Glib::Mutex::Lock lm (playlist_lock);
		for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ++i) {
			s.push_back (*i);
		}
		for (PlaylistList::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
			s.push_back (*i);
		}
	}
}

void
Session::track_playlist (bool inuse, boost::weak_ptr<Playlist> wpl)
{
	boost::shared_ptr<Playlist> pl(wpl.lock());

	if (!pl) {
		return;
	}

	PlaylistList::iterator x;

	if (pl->hidden()) {
		/* its not supposed to be visible */
		return;
	}

	{
		Glib::Mutex::Lock lm (playlist_lock);

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

void
Session::remove_playlist (boost::weak_ptr<Playlist> weak_playlist)
{
	if (_state_of_the_state & Deletion) {
		return;
	}

	boost::shared_ptr<Playlist> playlist (weak_playlist.lock());

	if (!playlist) {
		return;
	}

	{
		Glib::Mutex::Lock lm (playlist_lock);

		PlaylistList::iterator i;

		i = find (playlists.begin(), playlists.end(), playlist);
		if (i != playlists.end()) {
			playlists.erase (i);
		}

		i = find (unused_playlists.begin(), unused_playlists.end(), playlist);
		if (i != unused_playlists.end()) {
			unused_playlists.erase (i);
		}

	}

	set_dirty();

	PlaylistRemoved (playlist); /* EMIT SIGNAL */
}

void
Session::set_audition (boost::shared_ptr<Region> r)
{
	pending_audition_region = r;
	post_transport_work = PostTransportWork (post_transport_work | PostTransportAudition);
	schedule_butler_transport_work ();
}

void
Session::audition_playlist ()
{
	Event* ev = new Event (Event::Audition, Event::Add, Event::Immediate, 0, 0.0);
	ev->region.reset ();
	queue_event (ev);
}

void
Session::non_realtime_set_audition ()
{
	if (!pending_audition_region) {
		auditioner->audition_current_playlist ();
	} else {
		auditioner->audition_region (pending_audition_region);
		pending_audition_region.reset ();
	}
	AuditionActive (true); /* EMIT SIGNAL */
}

void
Session::audition_region (boost::shared_ptr<Region> r)
{
	Event* ev = new Event (Event::Audition, Event::Add, Event::Immediate, 0, 0.0);
	ev->region = r;
	queue_event (ev);
}

void
Session::cancel_audition ()
{
	if (auditioner->active()) {
		auditioner->cancel_audition ();
		AuditionActive (false); /* EMIT SIGNAL */
	}
}

bool
Session::RoutePublicOrderSorter::operator() (boost::shared_ptr<Route> a, boost::shared_ptr<Route> b)
{
	return a->order_key(N_("signal")) < b->order_key(N_("signal"));
}

void
Session::remove_empty_sounds ()
{
	vector<string> audio_filenames;

	get_files_in_directory (_session_dir->sound_path(), audio_filenames);

	Glib::Mutex::Lock lm (source_lock);

	TapeFileMatcher tape_file_matcher;

	remove_if (audio_filenames.begin(), audio_filenames.end(),
			sigc::mem_fun (tape_file_matcher, &TapeFileMatcher::matches));

	for (vector<string>::iterator i = audio_filenames.begin(); i != audio_filenames.end(); ++i) {

		sys::path audio_file_path (_session_dir->sound_path());

		audio_file_path /= *i;

		if (AudioFileSource::is_empty (*this, audio_file_path.to_string())) {

			try
			{
				sys::remove (audio_file_path);
				const string peakfile = peak_path (audio_file_path.to_string());
				sys::remove (peakfile);
			}
			catch (const sys::filesystem_error& err)
			{
				error << err.what() << endmsg;
			}
		}
	}
}

bool
Session::is_auditioning () const
{
	/* can be called before we have an auditioner object */
	if (auditioner) {
		return auditioner->active();
	} else {
		return false;
	}
}

void
Session::set_all_solo (bool yn)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if (!(*i)->is_hidden()) {
			(*i)->set_solo (yn, this);
		}
	}

	set_dirty();
}

void
Session::set_all_mute (bool yn)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		if (!(*i)->is_hidden()) {
			(*i)->set_mute (yn, this);
		}
	}

	set_dirty();
}

uint32_t
Session::n_diskstreams () const
{
	uint32_t n = 0;

	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::const_iterator i = dsl->begin(); i != dsl->end(); ++i) {
		if (!(*i)->hidden()) {
			n++;
		}
	}
	return n;
}

void
Session::graph_reordered ()
{
	/* don't do this stuff if we are setting up connections
	   from a set_state() call or creating new tracks.
	*/

	if (_state_of_the_state & InitialConnecting) {
		return;
	}

	/* every track/bus asked for this to be handled but it was deferred because
	   we were connecting. do it now.
	*/

	request_input_change_handling ();

	resort_routes ();

	/* force all diskstreams to update their capture offset values to
	   reflect any changes in latencies within the graph.
	*/

	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		(*i)->set_capture_offset ();
	}
}

void
Session::record_disenable_all ()
{
	record_enable_change_all (false);
}

void
Session::record_enable_all ()
{
	record_enable_change_all (true);
}

void
Session::record_enable_change_all (bool yn)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		boost::shared_ptr<Track> t;

		if ((t = boost::dynamic_pointer_cast<Track>(*i)) != 0) {
			t->set_record_enable (yn, this);
		}
	}

	/* since we don't keep rec-enable state, don't mark session dirty */
}

void
Session::add_processor (Processor* processor)
{
	processor->GoingAway.connect (sigc::bind (mem_fun (*this, &Session::remove_processor), processor));
	set_dirty();
}

void
Session::remove_processor (Processor* processor)
{
	Send* send;
	Return* retrn;
	PortInsert* port_insert;

	if ((port_insert = dynamic_cast<PortInsert *> (processor)) != 0) {
		insert_bitset[port_insert->bit_slot()] = false;
	} else if ((send = dynamic_cast<Send *> (processor)) != 0) {
		send_bitset[send->bit_slot()] = false;
	} else if ((retrn = dynamic_cast<Return *> (processor)) != 0) {
		return_bitset[send->bit_slot()] = false;
	}

	set_dirty();
}

nframes_t
Session::available_capture_duration ()
{
	float sample_bytes_on_disk = 4.0; // keep gcc happy

	switch (config.get_native_file_data_format()) {
	case FormatFloat:
		sample_bytes_on_disk = 4.0;
		break;

	case FormatInt24:
		sample_bytes_on_disk = 3.0;
		break;

	case FormatInt16:
		sample_bytes_on_disk = 2.0;
		break;

	default:
		/* impossible, but keep some gcc versions happy */
		fatal << string_compose (_("programming error: %1"),
					 X_("illegal native file data format"))
		      << endmsg;
		/*NOTREACHED*/
	}

	double scale = 4096.0 / sample_bytes_on_disk;

	if (_total_free_4k_blocks * scale > (double) max_frames) {
		return max_frames;
	}

	return (nframes_t) floor (_total_free_4k_blocks * scale);
}

void
Session::add_bundle (shared_ptr<Bundle> bundle)
{
	{
		RCUWriter<BundleList> writer (_bundles);
		boost::shared_ptr<BundleList> b = writer.get_copy ();
		b->push_back (bundle);
	}

	BundleAdded (bundle); /* EMIT SIGNAL */

	set_dirty();
}

void
Session::remove_bundle (shared_ptr<Bundle> bundle)
{
	bool removed = false;

	{
		RCUWriter<BundleList> writer (_bundles);
		boost::shared_ptr<BundleList> b = writer.get_copy ();
		BundleList::iterator i = find (b->begin(), b->end(), bundle);

		if (i != b->end()) {
			b->erase (i);
			removed = true;
		}
	}

	if (removed) {
		 BundleRemoved (bundle); /* EMIT SIGNAL */
	}

	set_dirty();
}

shared_ptr<Bundle>
Session::bundle_by_name (string name) const
{
	boost::shared_ptr<BundleList> b = _bundles.reader ();
	
	for (BundleList::const_iterator i = b->begin(); i != b->end(); ++i) {
		if ((*i)->name() == name) {
			return* i;
		}
	}

	return boost::shared_ptr<Bundle> ();
}

void
Session::tempo_map_changed (Change ignored)
{
	clear_clicks ();

	for (PlaylistList::iterator i = playlists.begin(); i != playlists.end(); ++i) {
		(*i)->update_after_tempo_map_change ();
	}

	for (PlaylistList::iterator i = unused_playlists.begin(); i != unused_playlists.end(); ++i) {
		(*i)->update_after_tempo_map_change ();
	}

	set_dirty ();
}

/** Ensures that all buffers (scratch, send, silent, etc) are allocated for
 * the given count with the current block size.
 */
void
Session::ensure_buffers (ChanCount howmany)
{
	if (current_block_size == 0) {
		return; // too early? (is this ok?)
	}

	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
		size_t count = std::max(_scratch_buffers->available().get(*t), howmany.get(*t));
		_scratch_buffers->ensure_buffers (*t, count, _engine.raw_buffer_size(*t));
		_mix_buffers->ensure_buffers (*t, count, _engine.raw_buffer_size(*t));
		_silent_buffers->ensure_buffers (*t, count, _engine.raw_buffer_size(*t));
	}

	allocate_pan_automation_buffers (current_block_size, howmany.n_audio(), false);
}

void
Session::ensure_buffer_set(BufferSet& buffers, const ChanCount& count)
{
	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
		buffers.ensure_buffers(*t, count.get(*t), _engine.raw_buffer_size(*t));
	}
}

uint32_t
Session::next_insert_id ()
{
	/* this doesn't really loop forever. just think about it */

	while (true) {
		for (boost::dynamic_bitset<uint32_t>::size_type n = 0; n < insert_bitset.size(); ++n) {
			if (!insert_bitset[n]) {
				insert_bitset[n] = true;
				return n;

			}
		}

		/* none available, so resize and try again */

		insert_bitset.resize (insert_bitset.size() + 16, false);
	}
}

uint32_t
Session::next_send_id ()
{
	/* this doesn't really loop forever. just think about it */

	while (true) {
		for (boost::dynamic_bitset<uint32_t>::size_type n = 0; n < send_bitset.size(); ++n) {
			if (!send_bitset[n]) {
				send_bitset[n] = true;
				return n;

			}
		}

		/* none available, so resize and try again */

		send_bitset.resize (send_bitset.size() + 16, false);
	}
}

uint32_t
Session::next_return_id ()
{
	/* this doesn't really loop forever. just think about it */

	while (true) {
		for (boost::dynamic_bitset<uint32_t>::size_type n = 0; n < return_bitset.size(); ++n) {
			if (!return_bitset[n]) {
				return_bitset[n] = true;
				return n;

			}
		}

		/* none available, so resize and try again */

		return_bitset.resize (return_bitset.size() + 16, false);
	}
}

void
Session::mark_send_id (uint32_t id)
{
	if (id >= send_bitset.size()) {
		send_bitset.resize (id+16, false);
	}
	if (send_bitset[id]) {
		warning << string_compose (_("send ID %1 appears to be in use already"), id) << endmsg;
	}
	send_bitset[id] = true;
}

void
Session::mark_return_id (uint32_t id)
{
	if (id >= return_bitset.size()) {
		return_bitset.resize (id+16, false);
	}
	if (return_bitset[id]) {
		warning << string_compose (_("return ID %1 appears to be in use already"), id) << endmsg;
	}
	return_bitset[id] = true;
}

void
Session::mark_insert_id (uint32_t id)
{
	if (id >= insert_bitset.size()) {
		insert_bitset.resize (id+16, false);
	}
	if (insert_bitset[id]) {
		warning << string_compose (_("insert ID %1 appears to be in use already"), id) << endmsg;
	}
	insert_bitset[id] = true;
}

/* Named Selection management */

NamedSelection *
Session::named_selection_by_name (string name)
{
	Glib::Mutex::Lock lm (named_selection_lock);
	for (NamedSelectionList::iterator i = named_selections.begin(); i != named_selections.end(); ++i) {
		if ((*i)->name == name) {
			return* i;
		}
	}
	return 0;
}

void
Session::add_named_selection (NamedSelection* named_selection)
{
	{
		Glib::Mutex::Lock lm (named_selection_lock);
		named_selections.insert (named_selections.begin(), named_selection);
	}

	for (list<boost::shared_ptr<Playlist> >::iterator i = named_selection->playlists.begin(); i != named_selection->playlists.end(); ++i) {
		add_playlist (*i);
	}

	set_dirty();

	NamedSelectionAdded (); /* EMIT SIGNAL */
}

void
Session::remove_named_selection (NamedSelection* named_selection)
{
	bool removed = false;

	{
		Glib::Mutex::Lock lm (named_selection_lock);

		NamedSelectionList::iterator i = find (named_selections.begin(), named_selections.end(), named_selection);

		if (i != named_selections.end()) {
			delete (*i);
			named_selections.erase (i);
			set_dirty();
			removed = true;
		}
	}

	if (removed) {
		 NamedSelectionRemoved (); /* EMIT SIGNAL */
	}
}

void
Session::reset_native_file_format ()
{
	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader();

	for (DiskstreamList::iterator i = dsl->begin(); i != dsl->end(); ++i) {
		(*i)->reset_write_sources (false);
	}
}

bool
Session::route_name_unique (string n) const
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::const_iterator i = r->begin(); i != r->end(); ++i) {
		if ((*i)->name() == n) {
			return false;
		}
	}

	return true;
}

bool
Session::route_name_internal (string n) const
{
	if (auditioner && auditioner->name() == n) {
		return true;
	}

	if (_click_io && _click_io->name() == n) {
		return true;
	}

	return false;
}

uint32_t
Session::n_playlists () const
{
	Glib::Mutex::Lock lm (playlist_lock);
	return playlists.size();
}

void
Session::allocate_pan_automation_buffers (nframes_t nframes, uint32_t howmany, bool force)
{
	if (!force && howmany <= _npan_buffers) {
		return;
	}

	if (_pan_automation_buffer) {

		for (uint32_t i = 0; i < _npan_buffers; ++i) {
			delete [] _pan_automation_buffer[i];
		}

		delete [] _pan_automation_buffer;
	}

	_pan_automation_buffer = new pan_t*[howmany];

	for (uint32_t i = 0; i < howmany; ++i) {
		_pan_automation_buffer[i] = new pan_t[nframes];
	}

	_npan_buffers = howmany;
}

int
Session::freeze (InterThreadInfo& itt)
{
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {

		boost::shared_ptr<Track> t;

		if ((t = boost::dynamic_pointer_cast<Track>(*i)) != 0) {
			/* XXX this is wrong because itt.progress will keep returning to zero at the start
			   of every track.
			*/
			t->freeze (itt);
		}
	}

	return 0;
}

boost::shared_ptr<Region>
Session::write_one_track (AudioTrack& track, nframes_t start, nframes_t end, 	
			  bool overwrite, vector<boost::shared_ptr<Source> >& srcs, 
			  InterThreadInfo& itt, bool enable_processing)
{
	boost::shared_ptr<Region> result;
	boost::shared_ptr<Playlist> playlist;
	boost::shared_ptr<AudioFileSource> fsource;
	uint32_t x;
	char buf[PATH_MAX+1];
	ChanCount nchans(track.audio_diskstream()->n_channels());
	nframes_t position;
	nframes_t this_chunk;
	nframes_t to_do;
	BufferSet buffers;
	SessionDirectory sdir(get_best_session_directory_for_new_source ());
	const string sound_dir = sdir.sound_path().to_string();
	nframes_t len = end - start;

	if (end <= start) {
		error << string_compose (_("Cannot write a range where end <= start (e.g. %1 <= %2)"),
					 end, start) << endmsg;
		return result;
	}

	// any bigger than this seems to cause stack overflows in called functions
	const nframes_t chunk_size = (128 * 1024)/4;

	// block all process callback handling

	block_processing ();

	/* call tree *MUST* hold route_lock */

	if ((playlist = track.diskstream()->playlist()) == 0) {
		goto out;
	}

	/* external redirects will be a problem */

	if (track.has_external_redirects()) {
		goto out;
	}

	for (uint32_t chan_n=0; chan_n < nchans.n_audio(); ++chan_n) {

		for (x = 0; x < 99999; ++x) {
			snprintf (buf, sizeof(buf), "%s/%s-%d-bounce-%" PRIu32 ".wav", sound_dir.c_str(), playlist->name().c_str(), chan_n, x+1);
			if (access (buf, F_OK) != 0) {
				break;
			}
		}

		if (x == 99999) {
			error << string_compose (_("too many bounced versions of playlist \"%1\""), playlist->name()) << endmsg;
			goto out;
		}

		try {
			fsource = boost::dynamic_pointer_cast<AudioFileSource> (
				SourceFactory::createWritable (DataType::AUDIO, *this, buf, true, false, frame_rate()));
		}

		catch (failed_constructor& err) {
			error << string_compose (_("cannot create new audio file \"%1\" for %2"), buf, track.name()) << endmsg;
			goto out;
		}

		srcs.push_back (fsource);
	}

	/* XXX need to flush all redirects */

	position = start;
	to_do = len;

	/* create a set of reasonably-sized buffers */
	buffers.ensure_buffers(DataType::AUDIO, nchans.n_audio(), chunk_size);
	buffers.set_count(nchans);

	for (vector<boost::shared_ptr<Source> >::iterator src=srcs.begin(); src != srcs.end(); ++src) {
		boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(*src);
		if (afs)
			afs->prepare_for_peakfile_writes ();
	}

	while (to_do && !itt.cancel) {

		this_chunk = min (to_do, chunk_size);

		if (track.export_stuff (buffers, start, this_chunk, enable_processing)) {
			goto out;
		}

		uint32_t n = 0;
		for (vector<boost::shared_ptr<Source> >::iterator src=srcs.begin(); src != srcs.end(); ++src, ++n) {
			boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(*src);

			if (afs) {
				if (afs->write (buffers.get_audio(n).data(), this_chunk) != this_chunk) {
					goto out;
				}
			}
		}

		start += this_chunk;
		to_do -= this_chunk;

		itt.progress = (float) (1.0 - ((double) to_do / len));

	}

	if (!itt.cancel) {

		time_t now;
		struct tm* xnow;
		time (&now);
		xnow = localtime (&now);

		for (vector<boost::shared_ptr<Source> >::iterator src=srcs.begin(); src != srcs.end(); ++src) {
			boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(*src);

			if (afs) {
				afs->update_header (position, *xnow, now);
				afs->flush_header ();
			}
		}

		/* construct a region to represent the bounced material */

		result = RegionFactory::create (srcs, 0,
				srcs.front()->length(srcs.front()->timeline_position()), 
				region_name_from_path (srcs.front()->name(), true));
	}

  out:
	if (!result) {
		for (vector<boost::shared_ptr<Source> >::iterator src = srcs.begin(); src != srcs.end(); ++src) {
			boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(*src);

			if (afs) {
				afs->mark_for_remove ();
			}
			
			(*src)->drop_references ();
		}

	} else {
		for (vector<boost::shared_ptr<Source> >::iterator src = srcs.begin(); src != srcs.end(); ++src) {
			boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(*src);

			if (afs)
				afs->done_with_peakfile_writes ();
		}
	}

	unblock_processing ();

	return result;
}

BufferSet&
Session::get_silent_buffers (ChanCount count)
{
	assert(_silent_buffers->available() >= count);
	_silent_buffers->set_count(count);

	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
		for (size_t i= 0; i < count.get(*t); ++i) {
			_silent_buffers->get(*t, i).clear();
		}
	}

	return *_silent_buffers;
}

BufferSet&
Session::get_scratch_buffers (ChanCount count)
{
	if (count != ChanCount::ZERO) {
		assert(_scratch_buffers->available() >= count);
		_scratch_buffers->set_count(count);
	} else {
		_scratch_buffers->set_count (_scratch_buffers->available());
	}

	return *_scratch_buffers;
}

BufferSet&
Session::get_mix_buffers (ChanCount count)
{
	assert(_mix_buffers->available() >= count);
	_mix_buffers->set_count(count);
	return *_mix_buffers;
}

uint32_t
Session::ntracks () const
{
	uint32_t n = 0;
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::const_iterator i = r->begin(); i != r->end(); ++i) {
		if (boost::dynamic_pointer_cast<Track> (*i)) {
			++n;
		}
	}

	return n;
}

uint32_t
Session::nbusses () const
{
	uint32_t n = 0;
	shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::const_iterator i = r->begin(); i != r->end(); ++i) {
		if (boost::dynamic_pointer_cast<Track>(*i) == 0) {
			++n;
		}
	}

	return n;
}

void
Session::add_automation_list(AutomationList *al)
{
	automation_lists[al->id()] = al;
}

nframes_t
Session::compute_initial_length ()
{
	return _engine.frame_rate() * 60 * 5;
}

void
Session::sync_order_keys (const char* base)
{
	if (!Config->get_sync_all_route_ordering()) {
		/* leave order keys as they are */
		return;
	}

	boost::shared_ptr<RouteList> r = routes.reader ();

	for (RouteList::iterator i = r->begin(); i != r->end(); ++i) {
		(*i)->sync_order_keys (base);
	}

	Route::SyncOrderKeys (base); // EMIT SIGNAL
}


/** @return true if there is at least one record-enabled diskstream, otherwise false */
bool
Session::have_rec_enabled_diskstream () const
{
	return g_atomic_int_get (&_have_rec_enabled_diskstream) == 1;
}

/** Update the state of our rec-enabled diskstreams flag */
void
Session::update_have_rec_enabled_diskstream ()
{
	boost::shared_ptr<DiskstreamList> dsl = diskstreams.reader ();
	DiskstreamList::iterator i = dsl->begin ();
	while (i != dsl->end () && (*i)->record_enabled () == false) {
		++i;
	}

	int const old = g_atomic_int_get (&_have_rec_enabled_diskstream);

	g_atomic_int_set (&_have_rec_enabled_diskstream, i != dsl->end () ? 1 : 0);

	if (g_atomic_int_get (&_have_rec_enabled_diskstream) != old) {
		RecordStateChanged (); /* EMIT SIGNAL */
	}
}
