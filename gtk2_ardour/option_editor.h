/*
    Copyright (C) 2009 Paul Davis

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

#ifndef __gtk_ardour_option_editor_h__
#define __gtk_ardour_option_editor_h__

#include <gtkmm/notebook.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/table.h>
#include "ardour_dialog.h"

/** @file option_editor.h
 *  @brief Base class for option editing dialog boxes.
 *
 *  Code to provided the basis for dialogs which allow the user to edit options
 *  from an ARDOUR::Configuration class.
 *
 *  The idea is that we have an OptionEditor class which is the dialog box.
 *  This is essentially a GTK Notebook.  OptionEditorComponent objects can
 *  then be added to the OptionEditor, and these components are arranged on
 *  the pages of the Notebook.  There is also an OptionEditorComponent hierarchy
 *  here, providing things like boolean and combobox option components.
 *
 *  It is intended that OptionEditor be subclassed to implement a particular
 *  options dialog.
 */

namespace ARDOUR {
	class Configuration;
}

class OptionEditorPage;

/** Base class for components of an OptionEditor dialog */
class OptionEditorComponent
{
public:
	virtual ~OptionEditorComponent() {}

	/** Called when a configuration parameter's value has changed.
	 *  @param p parameter name
	 */
	virtual void parameter_changed (std::string const & p) = 0;

	/** Called to instruct the object to set its UI state from the configuration */
	virtual void set_state_from_config () = 0;

	/** Called to instruct the object to add itself to an OptionEditorPage */
	virtual void add_to_page (OptionEditorPage *) = 0;

	void add_widget_to_page (OptionEditorPage*, Gtk::Widget*);
	void add_widgets_to_page (OptionEditorPage*, Gtk::Widget*, Gtk::Widget*);
};

/** A component which provides a subheading within the dialog */
class OptionEditorHeading : public OptionEditorComponent
{
public:
	OptionEditorHeading (std::string const &);

	void parameter_changed (std::string const &) {}
	void set_state_from_config () {}
	void add_to_page (OptionEditorPage *);

private:
	Gtk::Label* _label; ///< the label used for the heading
};

/** A component which provides a box into which a subclass can put arbitrary widgets */
class OptionEditorBox : public OptionEditorComponent
{
public:

	/** Construct an OpenEditorBox */
	OptionEditorBox ()
	{
		_box = Gtk::manage (new Gtk::VBox);
		_box->set_spacing (4);
	}

	void parameter_changed (std::string const &) = 0;
	void set_state_from_config () = 0;
	void add_to_page (OptionEditorPage *);

protected:

	Gtk::VBox* _box; ///< constituent box for subclasses to add widgets to
};

/** Base class for components which provide UI to change an option */
class Option : public OptionEditorComponent {

public:
	/** Construct an Option.
	 *  @param i Option id (e.g. "plugins-stop-with-transport")
	 *  @param n User-visible name (e.g. "Stop plugins when the transport is stopped")
	 */
	Option (std::string const & i,
		std::string const & n
		)
		: _id (i),
		  _name (n)
	{}

	void parameter_changed (std::string const & p)
	{
		if (p == _id) {
			set_state_from_config ();
		}
	}

	virtual void set_state_from_config () = 0;
	virtual void add_to_page (OptionEditorPage*) = 0;

	std::string id () const {
		return _id;
	}

private:

	std::string _id;
	std::string _name;
};

/** Component which provides the UI to handle a boolean option using a GTK CheckButton */
class BoolOption : public Option {

public:

	BoolOption (std::string const &, std::string const &, sigc::slot<bool>, sigc::slot<bool, bool>);
	void set_state_from_config ();
	void add_to_page (OptionEditorPage*);

private:

	void toggled ();

	sigc::slot<bool> _get; ///< slot to get the configuration variable's value
	sigc::slot<bool, bool> _set;  ///< slot to set the configuration variable's value
	Gtk::CheckButton* _button; ///< UI button
};

/** Component which provides the UI to handle a string option using a GTK Entry */
class EntryOption : public Option {

public:

	EntryOption (std::string const &, std::string const &, sigc::slot<std::string>, sigc::slot<bool, std::string>);
	void set_state_from_config ();
	void add_to_page (OptionEditorPage*);

private:

	void activated ();

	sigc::slot<std::string> _get; ///< slot to get the configuration variable's value
	sigc::slot<bool, std::string> _set;  ///< slot to set the configuration variable's value
	Gtk::Label* _label; ///< UI label
	Gtk::Entry* _entry; ///< UI entry
};


/** Component which provides the UI to handle an enumerated option using a GTK CheckButton.
 *  The template parameter is the enumeration.
 */
template <class T>
class ComboOption : public Option {

public:

	/** Construct an ComboOption.
	 *  @param i id
	 *  @param n User-visible name.
	 *  @param g Slot to get the variable's value.
	 *  @param s Slot to set the variable's value.
	 */
	ComboOption (
		std::string const & i,
		std::string const & n,
		sigc::slot<T> g,
		sigc::slot<bool, T> s
		)
		: Option (i, n),
		  _get (g),
		  _set (s)
	{
		_label = manage (new Gtk::Label (n + ":"));
		_label->set_alignment (0, 0.5);
		_combo = manage (new Gtk::ComboBoxText);
		_combo->signal_changed().connect (sigc::mem_fun (*this, &ComboOption::changed));
	}

	void set_state_from_config () {
		uint32_t r = 0;
		while (r < _options.size() && _get () != _options[r]) {
			++r;
		}

		if (r < _options.size()) {
			_combo->set_active (r);
		}
	}

	void add_to_page (OptionEditorPage* p)
	{
		add_widgets_to_page (p, _label, _combo);
	}

	/** Add an allowed value for this option.
	 *  @param e Enumeration.
	 *  @param o User-visible name for this value.
	 */
	void add (T e, std::string const & o) {
		_options.push_back (e);
		_combo->append_text (o);
	}

	void clear () {
		_combo->clear_items();
		_options.clear ();
	}

	void changed () {
		uint32_t const r = _combo->get_active_row_number ();
		if (r < _options.size()) {
			_set (_options[r]);
		}
	}

	void set_sensitive (bool yn) {
		_combo->set_sensitive (yn);
	}

private:

	sigc::slot<T> _get;
	sigc::slot<bool, T> _set;
	Gtk::Label* _label;
	Gtk::ComboBoxText* _combo;
	std::vector<T> _options;
};


/** Component which provides the UI to handle an numeric option using a GTK SpinButton */
template <class T>
class SpinOption : public Option
{
public:
	/** Construct an SpinOption.
	 *  @param i id
	 *  @param n User-visible name.
	 *  @param g Slot to get the variable's value.
	 *  @param s Slot to set the variable's value.
	 *  @param min Variable minimum value.
	 *  @param max Variable maximum value.
	 *  @param step Step for the spin button.
	 *  @param page Page step for the spin button.
	 *  @param unit Unit name.
	 *  @param scale Scaling factor (such that for a value x in the spinbutton, x * scale is written to the config)
	 */
	SpinOption (
		std::string const & i,
		std::string const & n,
		sigc::slot<T> g,
		sigc::slot<bool, T> s,
		T min,
		T max,
		T step,
		T page,
		std::string const & unit = "",
		float scale = 1
		)
		: Option (i, n),
		  _get (g),
		  _set (s),
		  _scale (scale)
	{
		_label = manage (new Gtk::Label (n + ":"));
		_label->set_alignment (0, 0.5);

		_spin = manage (new Gtk::SpinButton);
		_spin->set_range (min, max);
		_spin->set_increments (step, page);

		_box = manage (new Gtk::HBox);
		_box->pack_start (*_spin, true, true);
		_box->set_spacing (4);
		if (unit.length()) {
			_box->pack_start (*manage (new Gtk::Label (unit)), false, false);
		}

		_spin->signal_value_changed().connect (sigc::mem_fun (*this, &SpinOption::changed));
	}

	void set_state_from_config ()
	{
		_spin->set_value (_get () / _scale);
	}

	void add_to_page (OptionEditorPage* p)
	{
		add_widgets_to_page (p, _label, _box);
	}

	void changed ()
	{
		_set (static_cast<T> (_spin->get_value ()) * _scale);
	}

private:
	sigc::slot<T> _get;
	sigc::slot<bool, T> _set;
	float _scale;
	Gtk::Label* _label;
	Gtk::HBox* _box;
	Gtk::SpinButton* _spin;
};

/** Class to represent a single page in an OptionEditor's notebook.
 *  Pages are laid out using a 3-column table; the 1st column is used
 *  to indent non-headings, and the 2nd and 3rd for actual content.
 */
class OptionEditorPage
{
public:
	OptionEditorPage (Gtk::Notebook&, std::string const &);

	Gtk::VBox box;
	Gtk::Table table;
	std::list<OptionEditorComponent*> components;
};

/** The OptionEditor dialog base class */
class OptionEditor : public ArdourDialog
{
public:
	OptionEditor (ARDOUR::Configuration *, std::string const &);
	~OptionEditor ();

	void add_option (std::string const &, OptionEditorComponent *);

protected:

	ARDOUR::Configuration* _config;

private:

	void parameter_changed (std::string const &);

	Gtk::Notebook _notebook;
	std::map<std::string, OptionEditorPage*> _pages;
};

#endif /* __gtk_ardour_option_editor_h__ */


