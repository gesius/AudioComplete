/*
    Copyright (C) 2001 Paul Davis

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

#include <getopt.h>
#include <string.h>
#include <iostream>
#include <cstdlib>

#include "ardour/debug.h"
#include "ardour/session.h"

#include "opts.h"

#include "i18n.h"

using namespace std;

string ARDOUR_COMMAND_LINE::session_name = "";
string ARDOUR_COMMAND_LINE::jack_client_name = "ardour";
bool  ARDOUR_COMMAND_LINE::show_key_actions = false;
bool ARDOUR_COMMAND_LINE::no_splash = true;
bool ARDOUR_COMMAND_LINE::just_version = false;
bool ARDOUR_COMMAND_LINE::use_vst = true;
bool ARDOUR_COMMAND_LINE::new_session = false;
char* ARDOUR_COMMAND_LINE::curvetest_file = 0;
bool ARDOUR_COMMAND_LINE::try_hw_optimization = true;
string ARDOUR_COMMAND_LINE::keybindings_path = ""; /* empty means use builtin default */
Glib::ustring ARDOUR_COMMAND_LINE::menus_file = "ardour.menus";
bool ARDOUR_COMMAND_LINE::finder_invoked_ardour = false;
string ARDOUR_COMMAND_LINE::immediate_save;

using namespace ARDOUR_COMMAND_LINE;

int
print_help (const char *execname)
{
	cout << _("Usage: ") << execname << " [OPTION]... [SESSION_NAME]\n\n"
	     << _("  [SESSION_NAME]              Name of session to load\n")
	     << _("  -v, --version               Show version information\n")
	     << _("  -h, --help                  Print this message\n")
	     << _("  -b, --bindings              Print all possible keyboard binding names\n")
	     << _("  -c, --name <name>           Use a specific jack client name, default is ardour\n")
	     << _("  -d, --disable-plugins       Disable all plugins in an existing session\n")
	     << _("  -D, --debug <options>       Set debug flags. Use \"-D list\" to see available options\n")
	     << _("  -n, --show-splash           Show splash screen\n")
	     << _("  -m, --menus file            Use \"file\" for Ardour menus\n")
	     << _("  -N, --new session-name      Create a new session from the command line\n")
	     << _("  -O, --no-hw-optimizations   Disable h/w specific optimizations\n")
	     << _("  -S, --sync                  Draw the gui synchronously \n")
#ifdef VST_SUPPORT
	     << _("  -V, --novst                 Do not use VST support\n")
#endif
	     << _("  -E, --save <file>           Load the specified session, save it to <file> and then quit\n")
	     << _("  -C, --curvetest filename    Curve algorithm debugger\n")
	     << _("  -k, --keybindings filename  Name of key bindings to load (default is ~/.ardour3/ardour.bindings)\n")
		;
	return 1;

}

static void
list_debug_options ()
{
	cerr << _("The following debug options are available. Separate multipe options with commas.\nNames are case-insensitive and can be abbreviated.") << "\n\n";
	cerr << "\tMidiSourceIO\n";
	cerr << "\tMidiPlaylistIO\n";
	cerr << "\tMidiDiskstreamIO\n";
	cerr << "\tSnapBBT\n";
	cerr << "\tConfiguration\n";
}

static int
parse_debug_options (const char* str)
{
	char* p;
	char* sp;
	uint64_t bits = 0;
	char* copy = strdup (str);

	p = strtok_r (copy, ",", &sp);

	while (p) {
		if (strcasecmp (p, "list") == 0) {
			list_debug_options ();
			free (copy);
			return 1;
		}

		if (strcasecmp (p, "all") == 0) {
			ARDOUR::set_debug_bits (~0ULL);
			free (copy);
			return 0;
		}

		if (strncasecmp (p, "midisourceio", strlen (p)) == 0) {
			bits |= ARDOUR::DEBUG::MidiSourceIO;
		} else if (strncasecmp (p, "midiplaylistio", strlen (p)) == 0) {
			bits |= ARDOUR::DEBUG::MidiPlaylistIO;
		} else if (strncasecmp (p, "mididiskstreamio", strlen (p)) == 0) {
			bits |= ARDOUR::DEBUG::MidiDiskstreamIO;
		} else if (strncasecmp (p, "snapbbt", strlen (p)) == 0) {
			bits |= ARDOUR::DEBUG::SnapBBT;
		} else if (strncasecmp (p, "configuration", strlen (p)) == 0) {
			bits |= ARDOUR::DEBUG::Configuration;
		}

		p = strtok_r (0, ",", &sp);
	}
	
	free (copy);
	ARDOUR::set_debug_bits (bits);
	return 0;
}


int
ARDOUR_COMMAND_LINE::parse_opts (int argc, char *argv[])
{
	const char *optstring = "bc:C:dD:hk:E:m:N:nOp:SU:vV";
	const char *execname = strrchr (argv[0], '/');

	if (getenv ("ARDOUR_SAE")) {
		menus_file = "ardour-sae.menus";
		keybindings_path = "SAE";
	}

	if (execname == 0) {
		execname = argv[0];
	} else {
		execname++;
	}

	const struct option longopts[] = {
		{ "version", 0, 0, 'v' },
		{ "help", 0, 0, 'h' },
		{ "bindings", 0, 0, 'b' },
		{ "debug", 1, 0, 'D' },
		{ "show-splash", 0, 0, 'n' },
		{ "menus", 1, 0, 'm' },
		{ "name", 1, 0, 'c' },
		{ "novst", 0, 0, 'V' },
		{ "new", 1, 0, 'N' },
		{ "no-hw-optimizations", 0, 0, 'O' },
		{ "sync", 0, 0, 'S' },
		{ "curvetest", 1, 0, 'C' },
		{ "save", 1, 0, 'E' },
		{ 0, 0, 0, 0 }
	};

	int option_index = 0;
	int c = 0;

	while (1) {
		c = getopt_long (argc, argv, optstring, longopts, &option_index);

		if (c == -1) {
			break;
		}

		switch (c) {
		case 0:
			break;

		case 'v':
			just_version = true;
			break;

		case 'h':
			print_help (execname);
			exit (0);
			break;
		case 'b':
			show_key_actions = true;
			break;

		case 'd':
			ARDOUR::Session::set_disable_all_loaded_plugins (true);
			break;

		case 'D':
			if (parse_debug_options (optarg)) {
				exit (0);
			}
			break;
			
		case 'm':
			menus_file = optarg;
			break;

		case 'n':
			no_splash = false;
			break;

		case 'p':
			//undocumented OS X finder -psn_XXXXX argument
			finder_invoked_ardour = true;
			break;

		case 'S':
		//	; just pass this through to gtk it will figure it out
			break;

		case 'N':
			new_session = true;
			session_name = optarg;
			break;

		case 'O':
			try_hw_optimization = false;
			break;

		case 'V':
#ifdef VST_SUPPORT
			use_vst = false;
#endif /* VST_SUPPORT */
			break;

		case 'c':
			jack_client_name = optarg;
			break;

		case 'C':
			curvetest_file = optarg;
			break;

		case 'k':
			keybindings_path = optarg;
			break;

		case 'E':
			immediate_save = optarg;
			break;

		default:
			return print_help(execname);
		}
	}

	if (optind < argc) {
		if (new_session) {
			cerr << "Illogical combination: you can either create a new session, or a load an existing session but not both!" << endl;
			return print_help(execname);
		}
		session_name = argv[optind++];
	}

	return 0;
}

