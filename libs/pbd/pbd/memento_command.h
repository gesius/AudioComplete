/* 
    Copyright (C) 2006 Paul Davis
    Author: Hans Fugal

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

#ifndef __lib_pbd_memento_command_h__
#define __lib_pbd_memento_command_h__

#include <iostream>

#include "pbd/command.h"
#include "pbd/stacktrace.h"
#include "pbd/xml++.h"
#include "pbd/demangle.h"

#include <sigc++/slot.h>
#include <typeinfo>

/** This command class is initialized with before and after mementos 
 * (from Stateful::get_state()), so undo becomes restoring the before
 * memento, and redo is restoring the after memento.
 */
template <class obj_T>
class MementoCommand : public Command
{
public:
	MementoCommand(obj_T& a_object, XMLNode* a_before, XMLNode* a_after) 
		: obj(a_object), before(a_before), after(a_after)
	{
		/* if the object dies, make sure that we die and that everyone knows about it */
		obj.Destroyed.connect_same_thread (obj_death_connection, boost::bind (&MementoCommand::object_died, this));
	}

	~MementoCommand () {
		drop_references ();
		delete before;
		delete after;
	}

	void object_died () {
		delete this;
	}

	void operator() () {
		if (after) {
			obj.set_state(*after, Stateful::current_state_version); 
		}
	}

	void undo() { 
		if (before) {
			obj.set_state(*before, Stateful::current_state_version); 
		}
	}

	virtual XMLNode &get_state() {
		std::string name;
		if (before && after) {
			name = "MementoCommand";
		} else if (before) {
			name = "MementoUndoCommand";
		} else {
			name = "MementoRedoCommand";
		}

		XMLNode* node = new XMLNode(name);

		node->add_property("obj_id", obj.id().to_s());
		node->add_property("type_name", demangled_name (obj));

		if (before) {
			node->add_child_copy(*before);
		}
		
		if (after) {
			node->add_child_copy(*after);
		}

		return *node;
	}

protected:
	obj_T&   obj;
	XMLNode* before;
	XMLNode* after;
	PBD::ScopedConnection obj_death_connection;
};

#endif // __lib_pbd_memento_h__
