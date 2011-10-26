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

#ifndef __ardour_ui_configuration_h__
#define __ardour_ui_configuration_h__

#include <sstream>
#include <ostream>
#include <iostream>

#include "pbd/stateful.h"
#include "pbd/xml++.h"
#include "ardour/configuration_variable.h"

template<class T>
class UIConfigVariable : public ARDOUR::ConfigVariableBase
{
  public:
	UIConfigVariable (std::string str) : ARDOUR::ConfigVariableBase (str) {}
	UIConfigVariable (std::string str, T val) : ARDOUR::ConfigVariableBase (str), value (val) {}

	bool set (T val) {
		if (val == value) {
			return false;
		}
		value = val;
		return true;
	}

	T get() const {
		return value;
	}

	std::string get_as_string () const {
		std::stringstream ss;
		ss << std::hex;
		ss.fill('0');
		ss.width(8);
		ss << value;
		return ss.str ();
	}

	void set_from_string (std::string const & s) {
		std::stringstream ss;
		ss << std::hex;
		ss << s;
		ss >> value;
	}

  protected:
	T get_for_save() { return value; }
	T value;
};

class UIConfiguration : public PBD::Stateful
{
  public:
	UIConfiguration();
	~UIConfiguration();

	std::map<std::string,UIConfigVariable<uint32_t> *> canvas_colors;

	bool dirty () const;
	void set_dirty ();

	int load_state ();
	int save_state ();
	int load_defaults ();

	int set_state (const XMLNode&, int version);
	XMLNode& get_state (void);
	XMLNode& get_variables (std::string);
	void set_variables (const XMLNode&);
	void pack_canvasvars ();

	uint32_t color_by_name (const std::string&);

	sigc::signal<void,const char*> ParameterChanged;

#undef  UI_CONFIG_VARIABLE
#undef  CANVAS_VARIABLE
#define UI_CONFIG_VARIABLE(Type,var,name,val) UIConfigVariable<Type> var;
#define CANVAS_VARIABLE(var,name) UIConfigVariable<uint32_t> var;
#include "ui_config_vars.h"
#include "canvas_vars.h"
#undef  UI_CONFIG_VARIABLE
#undef  CANVAS_VARIABLE

  private:
	XMLNode& state ();
	bool _dirty;
};

#endif /* __ardour_ui_configuration_h__ */

