/*
    Copyright (C) 1998-99 Paul Barton-Davis 

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

    $Id$
*/

#ifdef COMPILER_MSVC
#include <stdlib.h>
#include <stdio.h>
using PBD::readdir;
using PBD::opendir;
using PBD::closedir;
#define strtok_r strtok_s // @john: this should probably go to msvc_extra_headers/ardourext/misc.h.input instead of the current define there
#else
#include <dirent.h>
#include <cstdlib>
#include <cstdio>
#endif
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>

#include <glibmm/miscutils.h>

#include "pbd/error.h"
#include "pbd/pathexpand.h"
#include "pbd/pathscanner.h"

using namespace std;
using namespace PBD;

static
bool
regexp_filter (const string& str, void *arg)
{
	regex_t* pattern = (regex_t*)arg;
	return regexec (pattern, str.c_str(), 0, 0, 0) == 0;
}

vector<string>
PathScanner::operator() (const string &dirpath, const string &regexp,
			 bool match_fullpath, bool return_fullpath, 
			 long limit, bool recurse)

{
	int err;
	char msg[256];
	regex_t compiled_pattern;
	vector<string> result;

	if ((err = regcomp (&compiled_pattern, regexp.c_str(),
			    REG_EXTENDED|REG_NOSUB))) {
		
		regerror (err, &compiled_pattern,
			  msg, sizeof (msg));
		
		error << "Cannot compile soundfile regexp for use (" 
		      << msg 
		      << ")" 
		      << endmsg;
		
		return vector<string>();
	}
	
	result =  run_scan (dirpath,
	                    regexp_filter,
	                    &compiled_pattern,
	                    match_fullpath,
	                    return_fullpath,
	                    limit, recurse);

	regfree (&compiled_pattern);

	return result;
}	

vector<string>
PathScanner::run_scan (const string &dirpath, 
		       bool (*filter)(const string &, void *),
		       void *arg,
		       bool match_fullpath, bool return_fullpath,
		       long limit,
		       bool recurse)
{
	vector<string> result;
	run_scan_internal (result, dirpath, filter, arg, match_fullpath, return_fullpath, limit, recurse);
	return result;
}
	
void
PathScanner::run_scan_internal (vector<string>& result,
				const string &dirpath, 
				bool (*filter)(const string &, void *),
				void *arg,
				bool match_fullpath, bool return_fullpath,
				long limit,
				bool recurse)
{
	DIR *dir;
	struct dirent *finfo;
	char *pathcopy = strdup (search_path_expand (dirpath).c_str());
	char *thisdir;
	string fullpath;
	string search_str;
	long nfound = 0;
	char *saveptr;

	if ((thisdir = strtok_r (pathcopy, G_SEARCHPATH_SEPARATOR_S, &saveptr)) == 0 ||
	    strlen (thisdir) == 0) {
		free (pathcopy);
		return;
	}

	do {

		if ((dir = opendir (thisdir)) == 0) {
			continue;
		}
		
		while ((finfo = readdir (dir)) != 0) {

			if ((finfo->d_name[0] == '.' && finfo->d_name[1] == '\0') ||
			    (finfo->d_name[0] == '.' && finfo->d_name[1] == '.' && finfo->d_name[2] == '\0')) {
				continue;
			}
                        
                        fullpath = Glib::build_filename (thisdir, finfo->d_name);

			struct stat statbuf;
			if (stat (fullpath.c_str(), &statbuf) < 0) {
				continue;
			}

			if (statbuf.st_mode & S_IFDIR && recurse) {
				run_scan_internal (result, fullpath, filter, arg, match_fullpath, return_fullpath, limit, recurse);
			} else {
				
				if (match_fullpath) {
					search_str = fullpath;
				} else {
					search_str = finfo->d_name;
				}
				
				if (!filter(search_str, arg)) {
					continue;
				}

				if (return_fullpath) {
					result.push_back(fullpath);
				} else {
					result.push_back(finfo->d_name);
				} 
				
				nfound++;
			}
		}
		closedir (dir);
		
	} while ((limit < 0 || (nfound < limit)) && (thisdir = strtok_r (0, G_SEARCHPATH_SEPARATOR_S, &saveptr)));

	free (pathcopy);
	return;
}

string
PathScanner::find_first (const string &dirpath,
			 const string &regexp,
			 bool match_fullpath,
			 bool return_fullpath)
{
	vector<string> res;
	int err;
	char msg[256];
	regex_t compiled_pattern;

	if ((err = regcomp (&compiled_pattern, regexp.c_str(),
			    REG_EXTENDED|REG_NOSUB))) {
		
		regerror (err, &compiled_pattern,
			  msg, sizeof (msg));
		
		error << "Cannot compile soundfile regexp for use (" << msg << ")" << endmsg;

		return 0;
	}
	
	run_scan_internal (res, dirpath,
	                   &regexp_filter,
			   &compiled_pattern,
	                   match_fullpath,
	                   return_fullpath,
	                   1);
	
	regfree (&compiled_pattern);

	if (res.size() == 0) {
		return string();
	}

	return res.front();
}

string
PathScanner::find_first (const string &dirpath,
			 bool (*filter)(const string &, void *),
			 void * /*arg*/,
			 bool match_fullpath,
			 bool return_fullpath)
{
	vector<string> res;
	string ret;

	run_scan_internal (res,
	                   dirpath,
	                   filter,
	                   0,
	                   match_fullpath,
	                   return_fullpath, 1);
	
	if (res.size() == 0) {
		return string();
	}

	return res.front();
}
