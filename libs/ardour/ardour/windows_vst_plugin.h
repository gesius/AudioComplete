/*
    Copyright (C) 2004 Paul Davis

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

#ifndef __ardour_windows_vst_plugin_h__
#define __ardour_windows_vst_plugin_h__

#include "ardour/vst_plugin.h"

struct _VSTHandle;
typedef struct _VSTHandle VSTHandle;

namespace ARDOUR {
	
class AudioEngine;
class Session;

class WindowsVSTPlugin : public VSTPlugin
{
public:
	WindowsVSTPlugin (AudioEngine &, Session &, VSTHandle *);
	WindowsVSTPlugin (const WindowsVSTPlugin &);
	~WindowsVSTPlugin ();

	std::string state_node_name () const { return "windows-vst"; }
};

class WindowsVSTPluginInfo : public PluginInfo
{
public:
	WindowsVSTPluginInfo ();
	~WindowsVSTPluginInfo () {}

	PluginPtr load (Session& session);
};

} // namespace ARDOUR

#endif /* __ardour_vst_plugin_h__ */
