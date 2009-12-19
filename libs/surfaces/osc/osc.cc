/*
 * Copyright (C) 2006 Paul Davis
 *  
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *  
 */

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <algorithm>

#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <glibmm/miscutils.h>
#include <boost/signals2.hpp>

#include <pbd/pthread_utils.h>
#include <pbd/file_utils.h>
#include <pbd/filesystem.h>
#include <pbd/failed_constructor.h>

#include "ardour/session.h"
#include "ardour/route.h"
#include "ardour/audio_track.h"
#include "ardour/midi_track.h"
#include "ardour/dB.h"
#include "ardour/filesystem_paths.h"

#include "osc.h"
#include "osc_controllable.h"
#include "i18n.h"

using namespace ARDOUR;
using namespace std;
using namespace Glib;


#include "pbd/abstract_ui.cc" // instantiate template

#ifdef DEBUG
static void error_callback(int num, const char *m, const char *path)
{
	fprintf(stderr, "liblo server error %d in path %s: %s\n", num, path, m);
}
#else
static void error_callback(int, const char *, const char *)
{

}
#endif

OSC::OSC (Session& s, uint32_t port)
	: ControlProtocol (s, "OSC")
	, AbstractUI<OSCUIRequest> ("osc")
	, _port(port)
{
	_shutdown = false;
	_osc_server = 0;
	_osc_unix_server = 0;
	_namespace_root = "/ardour";
	_send_route_changes = true;

	/* glibmm hack */
	local_server = 0;
	remote_server = 0;

	// "Application Hooks"
	session_loaded (s);
	session->Exported.connect (*this, boost::bind (&OSC::session_exported, this, _1, _2));
}

OSC::~OSC()
{
	stop ();
}

void
OSC::do_request (OSCUIRequest* req)
{
	if (req->type == CallSlot) {

		call_slot (req->the_slot);

	} else if (req->type == Quit) {

		stop ();
	}
}

int
OSC::set_active (bool yn)
{
	if (yn) {
		return start ();
	} else {
		return stop ();
	}
}

bool
OSC::get_active () const
{
	return _osc_server != 0;
}

int 
OSC::set_feedback (bool yn)
{
	_send_route_changes = yn;
	return 0;
}

bool
OSC::get_feedback () const
{
	return _send_route_changes;
}

int
OSC::start ()
{
	char tmpstr[255];

	if (_osc_server) {
		/* already started */
		return 0;
	}
	
	for (int j=0; j < 20; ++j) {
		snprintf(tmpstr, sizeof(tmpstr), "%d", _port);
		
		if ((_osc_server = lo_server_new (tmpstr, error_callback))) {
			break;
		}
#ifdef DEBUG		
		cerr << "can't get osc at port: " << _port << endl;
#endif
		_port++;
		continue;
	}
	
#ifdef ARDOUR_OSC_UNIX_SERVER
	
	// APPEARS sluggish for now
	
	// attempt to create unix socket server too
	
	snprintf(tmpstr, sizeof(tmpstr), "/tmp/sooperlooper_XXXXXX");
	int fd = mkstemp(tmpstr);
	
	if (fd >= 0 ) {
		unlink (tmpstr);
		close (fd);
		
		_osc_unix_server = lo_server_new (tmpstr, error_callback);
		
		if (_osc_unix_server) {
			_osc_unix_socket_path = tmpstr;
		}
	}
#endif
	
	cerr << "OSC @ " << get_server_url () << endl;

	PBD::sys::path url_file;

	if (find_file_in_search_path (ardour_search_path() + system_config_search_path(),
				      "osc_url", url_file)) {
		_osc_url_file = url_file.to_string();
		ofstream urlfile;
		urlfile.open(_osc_url_file.c_str(), ios::trunc);
		if ( urlfile )
		{
			urlfile << get_server_url () << endl;
			urlfile.close();
		}
		else
		{  
			cerr << "Couldn't write '" <<  _osc_url_file << "'" <<endl;
		}
	}
	
	register_callbacks();
	
	// lo_server_thread_add_method(_sthread, NULL, NULL, OSC::_dummy_handler, this);

	/* startup the event loop thread */

	BaseUI::run ();

	return 0;
}

void
OSC::thread_init ()
{
	if (_osc_unix_server) {
		Glib::RefPtr<IOSource> src = IOSource::create (lo_server_get_socket_fd (_osc_unix_server), IO_IN|IO_HUP|IO_ERR);
		src->connect (sigc::bind (sigc::mem_fun (*this, &OSC::osc_input_handler), _osc_unix_server));
		src->attach (_main_loop->get_context());
		local_server = src->gobj();
		g_source_ref (local_server);
	}

	if (_osc_server) {
		Glib::RefPtr<IOSource> src  = IOSource::create (lo_server_get_socket_fd (_osc_server), IO_IN|IO_HUP|IO_ERR);
		src->connect (sigc::bind (sigc::mem_fun (*this, &OSC::osc_input_handler), _osc_server));
		src->attach (_main_loop->get_context());
		remote_server = src->gobj();
		g_source_ref (remote_server);
	}
}

int
OSC::stop ()
{	
	/* stop main loop */

	if (local_server) {
		g_source_destroy (local_server);
		g_source_unref (local_server);
		local_server = 0;
	}

	if (remote_server) {
		g_source_destroy (remote_server);
		g_source_unref (remote_server);
		remote_server = 0;
	}

	BaseUI::quit ();

	if (_osc_server) {
		int fd = lo_server_get_socket_fd(_osc_server);
		if (fd >=0) {
			close(fd);
		}
		lo_server_free (_osc_server);
		_osc_server = 0;
	}

	if (_osc_unix_server) {
		int fd = lo_server_get_socket_fd(_osc_unix_server);
		if (fd >=0) {
			close(fd);
		}
		lo_server_free (_osc_unix_server);
		_osc_unix_server = 0;
	}
	
	if (!_osc_unix_socket_path.empty()) {
		unlink (_osc_unix_socket_path.c_str());
	}
	
	if (!_osc_url_file.empty() ) {
		unlink (_osc_url_file.c_str() );
	}

	return 0;
}

void
OSC::register_callbacks()
{
	lo_server srvs[2];
	lo_server serv;

	srvs[0] = _osc_server;
	srvs[1] = _osc_unix_server;
	
	for (size_t i = 0; i < 2; ++i) {

		if (!srvs[i]) {
			continue;
		}

		serv = srvs[i];
		
		/* this is a special catchall handler */
		
		lo_server_add_method (serv, 0, 0, _catchall, this);

#define REGISTER_CALLBACK(serv,path,types, function) lo_server_add_method (serv, path, types, OSC::_ ## function, this)
		
		REGISTER_CALLBACK (serv, "/ardour/add_marker", "", add_marker);
		REGISTER_CALLBACK (serv, "/ardour/access_action", "s", access_action);
		REGISTER_CALLBACK (serv, "/ardour/loop_toggle", "", loop_toggle);
		REGISTER_CALLBACK (serv, "/ardour/goto_start", "", goto_start);
		REGISTER_CALLBACK (serv, "/ardour/goto_end", "", goto_end);
		REGISTER_CALLBACK (serv, "/ardour/rewind", "", rewind);
		REGISTER_CALLBACK (serv, "/ardour/ffwd", "", ffwd);
		REGISTER_CALLBACK (serv, "/ardour/transport_stop", "", transport_stop);
		REGISTER_CALLBACK (serv, "/ardour/transport_play", "", transport_play);
		REGISTER_CALLBACK (serv, "/ardour/set_transport_speed", "f", set_transport_speed);
		REGISTER_CALLBACK (serv, "/ardour/save_state", "", save_state);
		REGISTER_CALLBACK (serv, "/ardour/prev_marker", "", prev_marker);
		REGISTER_CALLBACK (serv, "/ardour/next_marker", "", next_marker);
		REGISTER_CALLBACK (serv, "/ardour/undo", "", undo);
		REGISTER_CALLBACK (serv, "/ardour/redo", "", redo);
		REGISTER_CALLBACK (serv, "/ardour/toggle_punch_in", "", toggle_punch_in);
		REGISTER_CALLBACK (serv, "/ardour/toggle_punch_out", "", toggle_punch_out);
		REGISTER_CALLBACK (serv, "/ardour/rec_enable_toggle", "", rec_enable_toggle);
		REGISTER_CALLBACK (serv, "/ardour/toggle_all_rec_enables", "", toggle_all_rec_enables);

		REGISTER_CALLBACK (serv, "/ardour/routes/mute", "ii", route_mute);
		REGISTER_CALLBACK (serv, "/ardour/routes/solo", "ii", route_solo);
		REGISTER_CALLBACK (serv, "/ardour/routes/recenable", "ii", route_recenable);
		REGISTER_CALLBACK (serv, "/ardour/routes/gainabs", "if", route_set_gain_abs);
		REGISTER_CALLBACK (serv, "/ardour/routes/gaindB", "if", route_set_gain_dB);

		
#if 0
		/* still not-really-standardized query interface */
		REGISTER_CALLBACK (serv, "/ardour/*/#current_value", "", current_value);
		REGISTER_CALLBACK (serv, "/ardour/set", "", set);
#endif

#if 0
		// un/register_update args= s:ctrl s:returl s:retpath
		lo_server_add_method(serv, "/register_update", "sss", OSC::global_register_update_handler, this);
		lo_server_add_method(serv, "/unregister_update", "sss", OSC::global_unregister_update_handler, this);
		lo_server_add_method(serv, "/register_auto_update", "siss", OSC::global_register_auto_update_handler, this);
		lo_server_add_method(serv, "/unregister_auto_update", "sss", OSC::_global_unregister_auto_update_handler, this);
#endif
	}
}

bool
OSC::osc_input_handler (IOCondition ioc, lo_server srv)
{
	if (ioc & ~IO_IN) {
		return false;
	}

	if (ioc & IO_IN) {
		lo_server_recv (srv);
	}

	return true;
}

std::string
OSC::get_server_url()
{
	string url;
	char * urlstr;

	if (_osc_server) {
		urlstr = lo_server_get_url (_osc_server);
		url = urlstr;
		free (urlstr);
	}
	
	return url;
}

std::string
OSC::get_unix_server_url()
{
	string url;
	char * urlstr;

	if (_osc_unix_server) {
		urlstr = lo_server_get_url (_osc_unix_server);
		url = urlstr;
		free (urlstr);
	}
	
	return url;
}


void
OSC::current_value_query (const char* path, size_t len, lo_arg **argv, int argc, lo_message msg)
{
	char* subpath;
	
	subpath = (char*) malloc (len-15+1);
	memcpy (subpath, path, len-15);
	subpath[len-15] = '\0';
	
	send_current_value (subpath, argv, argc, msg);
	
	free (subpath);
}

void
OSC::send_current_value (const char* path, lo_arg** argv, int argc, lo_message msg)
{
	if (!session) {
		return;
	}

	lo_message reply = lo_message_new ();
	boost::shared_ptr<Route> r;
	int id;

	lo_message_add_string (reply, path);
	
	if (argc == 0) {
		lo_message_add_string (reply, "bad syntax");
	} else {
		id = argv[0]->i;
		r = session->route_by_remote_id (id);

		if (!r) {
			lo_message_add_string (reply, "not found");
		} else {

			if (strcmp (path, "/routes/state") == 0) {
				
				if (boost::dynamic_pointer_cast<AudioTrack>(r)) {
					lo_message_add_string (reply, "AT");
				} else if (boost::dynamic_pointer_cast<MidiTrack>(r)) {
					lo_message_add_string (reply, "MT");
				} else {
					lo_message_add_string (reply, "B");
				}
				
				lo_message_add_string (reply, r->name().c_str());
				lo_message_add_int32 (reply, r->n_inputs().n_audio());
				lo_message_add_int32 (reply, r->n_outputs().n_audio());
				lo_message_add_int32 (reply, r->muted());
				lo_message_add_int32 (reply, r->soloed());
				
			} else if (strcmp (path, "/routes/mute") == 0) {
				
				lo_message_add_int32 (reply, (float) r->muted());
				
			} else if (strcmp (path, "/routes/solo") == 0) {
				
				lo_message_add_int32 (reply, r->soloed());
			}
		}
	}

	lo_send_message (lo_message_get_source (msg), "#reply", reply);
	lo_message_free (reply);
}
	
int
OSC::_catchall (const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data) 
{
	return ((OSC*)user_data)->catchall (path, types, argv, argc, data);
}

int
OSC::catchall (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg) 
{
	size_t len;
	int ret = 1; /* unhandled */

	cerr << "Received a message, path = " << path << " types = \"" 
	     << (types ? types : "NULL") << '"' << endl;

	/* 15 for /#current_value plus 2 for /<path> */

	len = strlen (path);

	if (len >= 17 && !strcmp (&path[len-15], "/#current_value")) {
		current_value_query (path, len, argv, argc, msg);
		ret = 0;

	} else if (strcmp (path, "/routes/listen") == 0) {
		
		cerr << "set up listener\n";

		lo_message reply = lo_message_new ();

		if (argc <= 0) {
			lo_message_add_string (reply, "syntax error");
		} else {
			for (int n = 0; n < argc; ++n) {

				boost::shared_ptr<Route> r = session->route_by_remote_id (argv[n]->i);
				
				if (!r) {
					lo_message_add_string (reply, "not found");
					cerr << "no such route\n";
					break;
				} else {			
					cerr << "add listener\n";
					listen_to_route (r, lo_message_get_source (msg));
					lo_message_add_int32 (reply, argv[n]->i);
				}
			}
		}

		lo_send_message (lo_message_get_source (msg), "#reply", reply);
		lo_message_free (reply);

	} else if (strcmp (path, "/routes/ignore") == 0) {

		for (int n = 0; n < argc; ++n) {

			boost::shared_ptr<Route> r = session->route_by_remote_id (argv[n]->i);
			
			if (r) {
				end_listen (r, lo_message_get_source (msg));
			}
		}
	}

	return ret;
}

void
OSC::listen_to_route (boost::shared_ptr<Route> route, lo_address addr)
{
	Controllables::iterator x;
	bool route_exists = false;

	cerr << "listen to route\n";

	/* avoid duplicate listens */
	
	for (x = controllables.begin(); x != controllables.end(); ++x) {
		
		OSCRouteControllable* rc;

		if ((rc = dynamic_cast<OSCRouteControllable*>(*x)) != 0) {
			
			if (rc->route() == route) {
				route_exists = true;
				
				/* XXX NEED lo_address_equal() */
				
				if (rc->address() == addr) {
					return;
				}
			}
		}
	}

	cerr << "listener binding to signals\n";

	OSCControllable* c;
	string path;

	path = X_("/route/solo");
	c = new OSCRouteControllable (addr, path, route->solo_control(), route);
	controllables.push_back (c);

	path = X_("/route/mute");
	c = new OSCRouteControllable (addr, path, route->mute_control(), route);
	controllables.push_back (c);

	path = X_("/route/gain");
	c = new OSCRouteControllable (addr, path, route->gain_control(), route);
	controllables.push_back (c);

	cerr << "Now have " << controllables.size() << " controllables\n";

	/* if there is no existing controllable related to this route, make sure we clean up
	   if it is ever deleted.
	*/
	
	if (!route_exists) {
		route->GoingAway.connect (*this, boost::bind (&OSC::drop_route, this, boost::weak_ptr<Route> (route)));
	}
}

void
OSC::drop_route (boost::weak_ptr<Route> wr)
{
	boost::shared_ptr<Route> r = wr.lock ();

	if (!r) {
		return;
	}

	for (Controllables::iterator x = controllables.begin(); x != controllables.end();) {

		OSCRouteControllable* rc;
		
		if ((rc = dynamic_cast<OSCRouteControllable*>(*x)) != 0) {
			if (rc->route() == r) {
				delete *x;
				x = controllables.erase (x);
			} else {
				++x;
			}
		} else {
			++x;
		}
	}
}

void
OSC::end_listen (boost::shared_ptr<Route> r, lo_address addr)
{
	Controllables::iterator x;

	for (x = controllables.begin(); x != controllables.end(); ++x) {

		OSCRouteControllable* rc;
		
		if ((rc = dynamic_cast<OSCRouteControllable*>(*x)) != 0) {

			/* XXX NEED lo_address_equal () */

			if (rc->route() == r && rc->address() == addr) {
				controllables.erase (x);
				return;
			}
		}
	}
}

// "Application Hook" Handlers //
void
OSC::session_loaded( Session& s ) {
	lo_address listener = lo_address_new( NULL, "7770" );
	lo_send( listener, "/session/loaded", "ss", s.path().c_str(), s.name().c_str() );
}

void
OSC::session_exported( std::string path, std::string name ) {
	lo_address listener = lo_address_new( NULL, "7770" );
	lo_send( listener, "/session/exported", "ss", path.c_str(), name.c_str() );
}

// end "Application Hook" Handlers //

/* path callbacks */

int 
OSC::current_value (const char */*path*/, const char */*types*/, lo_arg **/*argv*/, int /*argc*/, void */*data*/, void* /*user_data*/) 
{ 
#if 0
	const char* returl;

	if (argc < 3 || types == 0 || strlen (types) < 3 || types[0] != 's' || types[1] != 's' || types[2] != s) {
		return 1;
	}

	const char *returl = argv[1]->s;
	lo_address addr = find_or_cache_addr (returl);

	const char *retpath = argv[2]->s;

	
	if (strcmp (argv[0]->s, "transport_frame") == 0) {

		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}

	} else if (strcmp (argv[0]->s, "transport_speed") == 0) {
		
		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}
		
	} else if (strcmp (argv[0]->s, "transport_locked") == 0) {
		
		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}
		
	} else if (strcmp (argv[0]->s, "punch_in") == 0) {
		
		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}
		
	} else if (strcmp (argv[0]->s, "punch_out") == 0) {

		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}
		
	} else if (strcmp (argv[0]->s, "rec_enable") == 0) {
			
		if (session) {
			lo_send (addr, retpath, "i", session->transport_frame());
		}

	} else {

		/* error */
	}
#endif
	return 0;
}

int
OSC::route_mute (int rid, int yn)
{
	if (!session) return -1;

	boost::shared_ptr<Route> r = session->route_by_remote_id (rid);

	if (r) {
		r->set_mute (yn, this);
	}
	return 0;
}

int
OSC::route_solo (int rid, int yn)
{
	if (!session) return -1;

	boost::shared_ptr<Route> r = session->route_by_remote_id (rid);

	if (r) {
		r->set_solo (yn, this);
	}
	return 0;
}

int
OSC::route_recenable (int rid, int yn)
{
	if (!session) return -1;

	boost::shared_ptr<Route> r = session->route_by_remote_id (rid);

	if (r) {
		r->set_record_enable (yn, this);
	}
	return 0;
}

int
OSC::route_set_gain_abs (int rid, float level)
{
	if (!session) return -1;

	boost::shared_ptr<Route> r = session->route_by_remote_id (rid);

	if (r) {
		r->set_gain (level, this);
	}

	return 0;
}

int
OSC::route_set_gain_dB (int rid, float dB)
{
	if (!session) return -1;

	boost::shared_ptr<Route> r = session->route_by_remote_id (rid);

	if (r) {
		r->set_gain (dB_to_coefficient (dB), this);
	}
	
	return 0;
}

XMLNode& 
OSC::get_state () 
{
	return *(new XMLNode ("OSC"));
}
		
int 
OSC::set_state (const XMLNode&, int /*version*/)
{
	return 0;
}
