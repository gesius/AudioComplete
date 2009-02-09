/*
    Copyright (C) 2002-2009 Paul Davis 

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
#include <boost/shared_ptr.hpp>

#include "ardour/audio_track.h"
#include "ardour/audioengine.h"
#include "ardour/bundle.h"
#include "ardour/user_bundle.h"
#include "ardour/io_processor.h"
#include "ardour/midi_track.h"
#include "ardour/port.h"
#include "ardour/session.h"

#include "port_group.h"
#include "port_matrix.h"

#include "i18n.h"

using namespace std;
using namespace Gtk;

/** PortGroup constructor.
 * @param n Name.
 */
PortGroup::PortGroup (std::string const & n)
	: name (n), _visible (true)
{
	
}

/** Add a bundle to a group.
 *  @param b Bundle.
 */
void
PortGroup::add_bundle (boost::shared_ptr<ARDOUR::Bundle> b)
{
	assert (b.get());
	_bundles.push_back (b);

	_bundle_changed_connections[b] = 
		b->Changed.connect (sigc::mem_fun (*this, &PortGroup::bundle_changed));
	
	Modified ();
}

void
PortGroup::remove_bundle (boost::shared_ptr<ARDOUR::Bundle> b)
{
	assert (b.get());

	ARDOUR::BundleList::iterator i = std::find (_bundles.begin(), _bundles.end(), b);
	if (i == _bundles.end()) {
		return;
	}

	_bundles.erase (i);
	_bundle_changed_connections[b].disconnect ();
	
	Modified ();
}

void
PortGroup::bundle_changed (ARDOUR::Bundle::Change c)
{
	BundleChanged (c);
}


void
PortGroup::clear ()
{
	_bundles.clear ();

	for (ConnectionList::iterator i = _bundle_changed_connections.begin(); i != _bundle_changed_connections.end(); ++i) {

		i->second.disconnect ();

	}
	
	_bundle_changed_connections.clear ();

	Modified ();
}

bool
PortGroup::has_port (std::string const& p) const
{
	for (ARDOUR::BundleList::const_iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		if ((*i)->offers_port_alone (p)) {
			return true;
		}
	}

	return false;
}

boost::shared_ptr<ARDOUR::Bundle>
PortGroup::only_bundle ()
{
	assert (_bundles.size() == 1);
	return _bundles.front();
}


uint32_t
PortGroup::total_channels () const
{
	uint32_t n = 0;
	for (ARDOUR::BundleList::const_iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		n += (*i)->nchannels ();
	}

	return n;
}

	
/** PortGroupList constructor.
 */
PortGroupList::PortGroupList ()
	: _type (ARDOUR::DataType::AUDIO), _signals_suspended (false), _pending_change (false)
{
	
}

void
PortGroupList::set_type (ARDOUR::DataType t)
{
	_type = t;
	clear ();
}

/** Gather bundles from around the system and put them in this PortGroupList */
void
PortGroupList::gather (ARDOUR::Session& session, bool inputs)
{
	clear ();

	boost::shared_ptr<PortGroup> bus (new PortGroup (_("Bus")));
	boost::shared_ptr<PortGroup> track (new PortGroup (_("Track")));
	boost::shared_ptr<PortGroup> system (new PortGroup (_("System")));
	boost::shared_ptr<PortGroup> other (new PortGroup (_("Other")));

	/* Find the bundles for routes.  We use the RouteBundle class to join
	   the route's IO bundles and processor bundles together so that they
	   are presented as one bundle in the matrix. */

	boost::shared_ptr<ARDOUR::RouteList> routes = session.get_routes ();

	for (ARDOUR::RouteList::const_iterator i = routes->begin(); i != routes->end(); ++i) {

		boost::shared_ptr<RouteBundle> rb (
			new RouteBundle (
				inputs ? (*i)->bundle_for_inputs() : (*i)->bundle_for_outputs()
				)
			);

		uint32_t n = 0;
		while (1) {
			boost::shared_ptr<ARDOUR::Processor> p = (*i)->nth_processor (n);
			if (p == 0) {
				break;
			}

			boost::shared_ptr<ARDOUR::IOProcessor> iop = boost::dynamic_pointer_cast<ARDOUR::IOProcessor> (p);

			if (iop) {
				rb->add_processor_bundle (
					inputs ? iop->io()->bundle_for_inputs() : iop->io()->bundle_for_outputs()
					);
					
			}

			++n;
		}
			
		/* Work out which group to put this bundle in */
		boost::shared_ptr<PortGroup> g;
		if (_type == ARDOUR::DataType::AUDIO) {

			if (boost::dynamic_pointer_cast<ARDOUR::AudioTrack> (*i)) {
				g = track;
			} else if (!boost::dynamic_pointer_cast<ARDOUR::MidiTrack>(*i)) {
				g = bus;
			} 


		} else if (_type == ARDOUR::DataType::MIDI) {

			if (boost::dynamic_pointer_cast<ARDOUR::MidiTrack> (*i)) {
				g = track;
			}

			/* No MIDI busses yet */
		} 
			
		if (g) {
			g->add_bundle (rb);
		}
	}

	/* Bundles owned by the session.  We only add the mono ones and the User ones
	   otherwise there is duplication of the same ports within the matrix */
	
	boost::shared_ptr<ARDOUR::BundleList> b = session.bundles ();
	for (ARDOUR::BundleList::iterator i = b->begin(); i != b->end(); ++i) {
		if ((*i)->ports_are_inputs() == inputs && (*i)->type() == _type) {

			if ((*i)->nchannels() == 1 || boost::dynamic_pointer_cast<ARDOUR::UserBundle> (*i)) {
				system->add_bundle (*i);
			}
		
		}
	}

	/* Now find all other ports that we haven't thought of yet */

	std::vector<std::string> extra_system;
	std::vector<std::string> extra_other;

 	const char **ports = session.engine().get_ports ("", _type.to_jack_type(), inputs ? 
 							  JackPortIsInput : JackPortIsOutput);
 	if (ports) {

		int n = 0;
		string client_matching_string;

		client_matching_string = session.engine().client_name();
		client_matching_string += ':';

		while (ports[n]) {
			
			std::string const p = ports[n];

			if (!system->has_port(p) && !bus->has_port(p) && !track->has_port(p) && !other->has_port(p)) {
				
				if (port_has_prefix (p, "system:") ||
				    port_has_prefix (p, "alsa_pcm") ||
				    port_has_prefix (p, "ardour:")) {
					extra_system.push_back (p);
				} else {
					extra_other.push_back (p);
				}
			}
			
			++n;
		}

		free (ports);
	}

	if (!extra_system.empty()) {
		system->add_bundle (make_bundle_from_ports (extra_system, inputs));
	}

	if (!extra_other.empty()) {
		other->add_bundle (make_bundle_from_ports (extra_other, inputs));
	}

	add_group (system);
	add_group (bus);
	add_group (track);
	add_group (other);

	emit_changed ();
}

boost::shared_ptr<ARDOUR::Bundle>
PortGroupList::make_bundle_from_ports (std::vector<std::string> const & p, bool inputs) const
{
	boost::shared_ptr<ARDOUR::Bundle> b (new ARDOUR::Bundle ("", _type, inputs));

	std::string const pre = common_prefix (p);
	if (!pre.empty()) {
		b->set_name (pre.substr (0, pre.length() - 1));
	}

	for (uint32_t j = 0; j < p.size(); ++j) {
		b->add_channel (p[j].substr (pre.length()));
		b->set_port (j, p[j]);
	}

	return b;
}

bool
PortGroupList::port_has_prefix (const std::string& n, const std::string& p) const
{
	return n.substr (0, p.length()) == p;
}

std::string
PortGroupList::common_prefix_before (std::vector<std::string> const & p, std::string const & s) const
{
	/* we must have some strings and the first must contain the separator string */
	if (p.empty() || p[0].find_first_of (s) == std::string::npos) {
		return "";
	}

	/* prefix of the first string */
	std::string const fp = p[0].substr (0, p[0].find_first_of (s) + 1);

	/* see if the other strings also start with fp */
	uint32_t j = 1;
	while (j < p.size()) {
		if (p[j].substr (0, fp.length()) != fp) {
			break;
		}
		++j;
	}

	if (j != p.size()) {
		return "";
	}

	return fp;
}
	

std::string
PortGroupList::common_prefix (std::vector<std::string> const & p) const
{
	/* common prefix before '/' ? */
	std::string cp = common_prefix_before (p, "/");
	if (!cp.empty()) {
		return cp;
	}

	cp = common_prefix_before (p, ":");
	if (!cp.empty()) {
		return cp;
	}

	return "";
}

void
PortGroupList::clear ()
{
	_groups.clear ();

	for (std::vector<sigc::connection>::iterator i = _bundle_changed_connections.begin(); i != _bundle_changed_connections.end(); ++i) {
		i->disconnect ();
	}

	_bundle_changed_connections.clear ();

	emit_changed ();
}


ARDOUR::BundleList const &
PortGroupList::bundles () const
{
	_bundles.clear ();

	for (PortGroupList::List::const_iterator i = begin (); i != end (); ++i) {
		if ((*i)->visible()) {
			std::copy ((*i)->bundles().begin(), (*i)->bundles().end(), std::back_inserter (_bundles));
		}
	}

	return _bundles;
}

uint32_t
PortGroupList::total_visible_channels () const
{
	uint32_t n = 0;
	
	for (PortGroupList::List::const_iterator i = begin(); i != end(); ++i) {
		if ((*i)->visible()) {
			n += (*i)->total_channels ();
		}
	}

	return n;
}


void
PortGroupList::add_group (boost::shared_ptr<PortGroup> g)
{
	_groups.push_back (g);
	
	g->Modified.connect (sigc::mem_fun (*this, &PortGroupList::emit_changed));
	
	_bundle_changed_connections.push_back (
		g->BundleChanged.connect (sigc::hide (sigc::mem_fun (*this, &PortGroupList::emit_changed)))
		);

	emit_changed ();
}

void
PortGroupList::remove_bundle (boost::shared_ptr<ARDOUR::Bundle> b)
{
	for (List::iterator i = _groups.begin(); i != _groups.end(); ++i) {
		(*i)->remove_bundle (b);
	}

	emit_changed ();
}

void
PortGroupList::emit_changed ()
{
	if (_signals_suspended) {
		_pending_change = true;
	} else {
		Changed ();
	}
}
		
void
PortGroupList::suspend_signals ()
{
	_signals_suspended = true;
}

void
PortGroupList::resume_signals ()
{
	if (_pending_change) {
		Changed ();
		_pending_change = false;
	}

	_signals_suspended = false;
}

RouteBundle::RouteBundle (boost::shared_ptr<ARDOUR::Bundle> r)
	: _route (r)
{
	_route->Changed.connect (sigc::hide (sigc::mem_fun (*this, &RouteBundle::reread_component_bundles)));
	reread_component_bundles ();
}

void
RouteBundle::reread_component_bundles ()
{
	suspend_signals ();
	
	remove_channels ();

	set_name (_route->name());

	for (uint32_t i = 0; i < _route->nchannels(); ++i) {
		add_channel (_route->channel_name (i));
		PortList const & pl = _route->channel_ports (i);
		for (uint32_t j = 0; j < pl.size(); ++j) {
			add_port_to_channel (i, pl[j]);
		}
	}
		
	for (std::vector<boost::shared_ptr<ARDOUR::Bundle> >::iterator i = _processor.begin(); i != _processor.end(); ++i) {
		add_channels_from_bundle (*i);
	}

	resume_signals ();
}

void
RouteBundle::add_processor_bundle (boost::shared_ptr<ARDOUR::Bundle> p)
{
	p->Changed.connect (sigc::hide (sigc::mem_fun (*this, &RouteBundle::reread_component_bundles)));
	_processor.push_back (p);
	
	reread_component_bundles ();
}
	
