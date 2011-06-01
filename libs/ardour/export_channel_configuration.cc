/*
    Copyright (C) 2008 Paul Davis
    Author: Sakari Bergen

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

#include "ardour/export_channel_configuration.h"

#include "ardour/export_handler.h"
#include "ardour/export_filename.h"
#include "ardour/export_timespan.h"

#include "ardour/audio_port.h"
#include "ardour/export_failed.h"
#include "ardour/midi_port.h"
#include "ardour/session.h"
#include "ardour/audioengine.h"

#include "pbd/convert.h"
#include "pbd/pthread_utils.h"

using namespace PBD;

namespace ARDOUR
{

/* ExportChannelConfiguration */

ExportChannelConfiguration::ExportChannelConfiguration (Session & session) :
  session (session),
  split (false)
{

}

XMLNode &
ExportChannelConfiguration::get_state ()
{
	XMLNode * root = new XMLNode ("ExportChannelConfiguration");
	XMLNode * channel;

	root->add_property ("split", get_split() ? "true" : "false");
	root->add_property ("channels", to_string (get_n_chans(), std::dec));

	uint32_t i = 1;
	for (ExportChannelConfiguration::ChannelList::const_iterator c_it = channels.begin(); c_it != channels.end(); ++c_it) {
		channel = root->add_child ("Channel");
		if (!channel) { continue; }

		channel->add_property ("number", to_string (i, std::dec));
		(*c_it)->get_state (channel);

		++i;
	}

	return *root;
}

int
ExportChannelConfiguration::set_state (const XMLNode & root)
{
	XMLProperty const * prop;

	if ((prop = root.property ("split"))) {
		set_split (!prop->value().compare ("true"));
	}

	XMLNodeList channels = root.children ("Channel");
	for (XMLNodeList::iterator it = channels.begin(); it != channels.end(); ++it) {
		ExportChannelPtr channel (new PortExportChannel ());
		channel->set_state (*it, session);
		register_channel (channel);
	}

	return 0;
}

bool
ExportChannelConfiguration::all_channels_have_ports () const
{
	for (ChannelList::const_iterator it = channels.begin(); it != channels.end(); ++it) {
		if ((*it)->empty ()) { return false; }
	}

	return true;
}

void
ExportChannelConfiguration::configurations_for_files (std::list<boost::shared_ptr<ExportChannelConfiguration> > & configs)
{
	configs.clear ();

	if (!split) {
		configs.push_back (shared_from_this ());
		return;
	}

	for (ChannelList::const_iterator it = channels.begin (); it != channels.end (); ++it) {
		boost::shared_ptr<ExportChannelConfiguration> config (new ExportChannelConfiguration (session));
		config->set_name (_name);
		config->register_channel (*it);
		configs.push_back (config);
	}
}

} // namespace ARDOUR
