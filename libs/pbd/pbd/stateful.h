/*
    Copyright (C) 2000-2010 Paul Davis 

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

#ifndef __pbd_stateful_h__
#define __pbd_stateful_h__

#include <string>
#include <list>
#include <cassert>
#include "pbd/id.h"
#include "pbd/xml++.h"
#include "pbd/enumwriter.h"
#include "pbd/properties.h"

class XMLNode;

namespace PBD {

namespace sys {
	class path;
}

/** Base class for objects with saveable and undoable state */
class Stateful {
  public:
	Stateful ();
	virtual ~Stateful();

	virtual XMLNode& get_state (void) = 0;
	virtual int set_state (const XMLNode&, int version) = 0;
	/* derived types do not have to implement this, but probably should
	   give it serious attention.
	*/
	virtual PropertyChange set_property (const PropertyBase&) { return PropertyChange (0); }

	PropertyChange set_properties (const PropertyList&);

	void add_property (PropertyBase& s) {
		_properties.add (s);
	}

	/* Extra XML node: so that 3rd parties can attach state to the XMLNode
	   representing the state of this object.
	 */

	void add_extra_xml (XMLNode&);
	XMLNode *extra_xml (const std::string& str);

	const PBD::ID& id() const { return _id; }

	void clear_history ();
	std::pair<XMLNode *, XMLNode*> diff () const;
	void changed (PropertyChange&) const;

	static int current_state_version;
	static int loading_state_version;

  protected:

	void add_instant_xml (XMLNode&, const sys::path& directory_path);
	XMLNode *instant_xml (const std::string& str, const sys::path& directory_path);
	void add_properties (XMLNode &);
	/* derived types can call this from ::set_state() (or elsewhere)
	   to get basic property setting done.
	*/
	PropertyChange set_properties (XMLNode const &);

	
	/* derived classes can implement this to do cross-checking
	   of property values after either a PropertyList or XML 
	   driven property change.
	*/
	virtual void post_set () { };

	XMLNode *_extra_xml;
	XMLNode *_instant_xml;
	PBD::ID _id;

	std::string _xml_node_name; ///< name of node to use for this object in XML
	OwnedPropertyList _properties;
};

} // namespace PBD

#endif /* __pbd_stateful_h__ */

