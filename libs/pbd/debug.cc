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

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>

#include <boost/tokenizer.hpp>

#include "pbd/debug.h"

#include "i18n.h"

using namespace std;
using PBD::DebugBits;

static uint64_t _debug_bit = 0;

typedef std::map<const char*,DebugBits> DebugMap;

namespace PBD {
	DebugMap & _debug_bit_map()
	{
		static DebugMap map;
		return map;
	}
}

DebugBits PBD::DEBUG::Stateful = PBD::new_debug_bit ("stateful");
DebugBits PBD::DEBUG::Properties = PBD::new_debug_bit ("properties");
DebugBits PBD::DEBUG::FileManager = PBD::new_debug_bit ("filemanager");
DebugBits PBD::DEBUG::Pool = PBD::new_debug_bit ("pool");
DebugBits PBD::DEBUG::EventLoop = PBD::new_debug_bit ("eventloop");
DebugBits PBD::DEBUG::AbstractUI = PBD::new_debug_bit ("abstractui");
DebugBits PBD::DEBUG::FileUtils = PBD::new_debug_bit ("fileutils");
DebugBits PBD::DEBUG::Configuration = PBD::new_debug_bit ("configuration");

/* These are debug bits that are used by backends. Since these are loaded dynamically,
   after command-line parsing, defining them in code that is part of the backend
   doesn't make them available for command line parsing. Put them here.

   This is sort of a hack, because it means that the debug bits aren't defined
   with the code in which they are relevant. But providing access to debug bits
   from dynamically loaded code, for use in command line parsing, is way above the pay grade
   of this debug tracing scheme.
*/
DebugBits PBD::DEBUG::BackendMIDI = PBD::new_debug_bit ("BackendMIDI");
DebugBits PBD::DEBUG::BackendAudio = PBD::new_debug_bit ("BackendAudio");
DebugBits PBD::DEBUG::BackendTiming = PBD::new_debug_bit ("BackendTiming");
DebugBits PBD::DEBUG::BackendThreads = PBD::new_debug_bit ("BackendThreads");


DebugBits PBD::DEBUG::WavesMIDI = PBD::new_debug_bit ("WavesMIDI");
DebugBits PBD::DEBUG::WavesAudio = PBD::new_debug_bit ("WavesAudio");

DebugBits PBD::debug_bits;

DebugBits
PBD::new_debug_bit (const char* name)
{
        DebugBits ret;
        ret.set (_debug_bit++, 1);
        _debug_bit_map().insert (make_pair (name, ret));
        return ret;
}

void
PBD::debug_print (const char* prefix, string str)
{
	cerr << prefix << ": " << str;
}

int
PBD::parse_debug_options (const char* str)
{
	string in_str = str;
	typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
	boost::char_separator<char> sep (",");
	tokenizer tokens (in_str, sep);
	DebugBits bits;

	for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter) {
		if (*tok_iter == "list") {
			list_debug_options ();
			return 1;
		}

		if (*tok_iter == "all") {
			debug_bits.set (); /* sets all bits */
			return 0;
		}

		for (map<const char*,DebugBits>::iterator i = _debug_bit_map().begin(); i != _debug_bit_map().end(); ++i) {
			const char* cstr = (*tok_iter).c_str();

                        if (strncasecmp (cstr, i->first, strlen (cstr)) == 0) {
	                        bits |= i->second;
	                        cerr << i->first << " set ... debug bits now set to " << bits << " using " << i->second << endl;
                        }
                }
	}
	
	debug_bits = bits;
	
	return 0;
}

void
PBD::list_debug_options ()
{
	cout << _("The following debug options are available. Separate multiple options with commas.\nNames are case-insensitive and can be abbreviated.") << endl << endl;
	cout << '\t' << X_("all") << endl; 

	vector<string> options;

	for (map<const char*,DebugBits>::iterator i = _debug_bit_map().begin(); i != _debug_bit_map().end(); ++i) {
		options.push_back (i->first);
        }

	sort (options.begin(), options.end());

	for (vector<string>::iterator i = options.begin(); i != options.end(); ++i) {
                cout << "\t" << (*i) << endl;
	}
}
