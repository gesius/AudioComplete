/*
	Copyright (C) 2007 Tim Mayberry 

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

#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <giomm/file.h>

#include <cerrno>
#include <fstream>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/filesystem.h"
#include "pbd/error.h"
#include "pbd/compose.h"
#include "pbd/pathscanner.h"

#include "i18n.h"

using namespace std;

namespace PBD {

namespace sys {
	
path&
path::operator/=(const path& rhs)
{
	m_path = Glib::build_filename(m_path, rhs.m_path);
	return *this;
}

path&
path::operator/=(const string& rhs)
{
	m_path = Glib::build_filename(m_path, rhs);
	return *this;
}

path&
path::operator/=(const char* rhs)
{
	m_path = Glib::build_filename(m_path, rhs);
	return *this;
}

} // namespace sys

} // namespace PBD
