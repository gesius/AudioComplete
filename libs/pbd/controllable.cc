/*
    Copyright (C) 2000-2007 Paul Davis 

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

#include "pbd/controllable.h"
#include "pbd/enumwriter.h"
#include "pbd/xml++.h"
#include "pbd/error.h"

#include "i18n.h"

using namespace PBD;
using namespace std;

PBD::Signal1<void,Controllable*> Controllable::Destroyed;
PBD::Signal1<bool,Controllable*> Controllable::StartLearning;
PBD::Signal1<void,Controllable*> Controllable::StopLearning;
PBD::Signal3<void,Controllable*,int,int> Controllable::CreateBinding;
PBD::Signal1<void,Controllable*> Controllable::DeleteBinding;

Glib::StaticRWLock Controllable::registry_lock = GLIBMM_STATIC_RW_LOCK_INIT;
Controllable::Controllables Controllable::registry;
PBD::ScopedConnectionList registry_connections;

Controllable::Controllable (const string& name, Flag f)
	: _name (name)
	, _flags (f)
{
	add (*this);
}

void
Controllable::add (Controllable& ctl)
{
	using namespace boost;

	Glib::RWLock::WriterLock lm (registry_lock);
	registry.insert (&ctl);

	/* Controllable::remove() is static - no need to manage this connection */

	ctl.DropReferences.connect_same_thread (registry_connections, boost::bind (&Controllable::remove, &ctl));
}

void
Controllable::remove (Controllable* ctl)
{
	Glib::RWLock::WriterLock lm (registry_lock);

	for (Controllables::iterator i = registry.begin(); i != registry.end(); ++i) {
		if ((*i) == ctl) {
			registry.erase (i);
			break;
		}
	}
}

Controllable*
Controllable::by_id (const ID& id)
{
	Glib::RWLock::ReaderLock lm (registry_lock);

	for (Controllables::iterator i = registry.begin(); i != registry.end(); ++i) {
		if ((*i)->id() == id) {
			return (*i);
		}
	}
	return 0;
}

Controllable*
Controllable::by_name (const string& str)
{
	Glib::RWLock::ReaderLock lm (registry_lock);

	for (Controllables::iterator i = registry.begin(); i != registry.end(); ++i) {
		if ((*i)->_name == str) {
			return (*i);
		}
	}
	return 0;
}

XMLNode&
Controllable::get_state ()
{
	XMLNode* node = new XMLNode (X_("Controllable"));
	char buf[64];

	node->add_property (X_("name"), _name); // not reloaded from XML state, just there to look at
	_id.print (buf, sizeof (buf));
	node->add_property (X_("id"), buf);
	node->add_property (X_("flags"), enum_2_string (_flags));

	return *node;
}

int
Controllable::set_state (const XMLNode& node, int /*version*/)
{
	const XMLProperty* prop;

	if ((prop = node.property (X_("id"))) != 0) {
		_id = prop->value();
		return 0;
	} else {
		error << _("Controllable state node has no ID property") << endmsg;
		return -1;
	}

	if ((prop = node.property (X_("flags"))) != 0) {
		_flags = (Flag) string_2_enum (prop->value(), _flags);
	}
}

void
Controllable::set_flags (Flag f)
{
	_flags = f;
}
