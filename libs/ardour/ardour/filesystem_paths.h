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

#ifndef ARDOUR_FILESYSTEM_PATHS_INCLUDED
#define ARDOUR_FILESYSTEM_PATHS_INCLUDED

#include "pbd/search_path.h"

#include "ardour/libardour_visibility.h"

namespace ARDOUR {

	/**
	 * @return the path to the directory used to store user specific ardour
	 * configuration files.
	 * @post user_config_directory() exists
	 */
	LIBARDOUR_API std::string user_config_directory ();

	/**
	 * @return the path to the directory used to store user specific
	 * caches (e.g. plugin indices, blacklist/whitelist)
	 * it defaults to XDG_CACHE_HOME
	 */
	LIBARDOUR_API std::string user_cache_directory ();


	/**
	 * @return the path to the directory that contains the system wide ardour
	 * modules.
	 */
	LIBARDOUR_API std::string ardour_dll_directory ();

	/**
	 * @return the search path to be used when looking for per-system
	 * configuration files. This may include user configuration files.
	 */
	LIBARDOUR_API PBD::Searchpath ardour_config_search_path ();

	/**
	 * @return the search path to be used when looking for data files
	 * that could be shared by systems (h/w and configuration independent
	 * files, such as icons, XML files, etc)
	 */
	LIBARDOUR_API PBD::Searchpath ardour_data_search_path ();

} // namespace ARDOUR

#endif
