// -*- c++ -*-
// Generated by gtkmmproc -- DO NOT MODIFY!
#ifndef _GTKMM_TEXTTAGTABLE_H
#define _GTKMM_TEXTTAGTABLE_H


#include <glibmm.h>

/* $Id$ */

/* texttagtable.h
 * 
 * Copyright (C) 1998-2002 The gtkmm Development Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gtkmm/object.h>
#include <gtkmm/texttag.h>


#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct _GtkTextTagTable GtkTextTagTable;
typedef struct _GtkTextTagTableClass GtkTextTagTableClass;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */


namespace Gtk
{ class TextTagTable_Class; } // namespace Gtk
namespace Gtk
{

class TextTag;

/** Typedefed as Gtk::TextBuffer::TagTable. A Collection of @link Gtk::TextTag Gtk::TextBuffer::Tags@endlink that can be used together.
 *
 * A tag table defines a set of @link Gtk::TextTag Gtk::TextBuffer::Tags@endlink that can be used together. Each buffer has one tag
 * table associated with it; only tags from that tag table can be used with the buffer. A single tag table can be shared between
 * multiple buffers, however.
 *
 * @ingroup TextView
 */

class TextTagTable : public Glib::Object
{
   
#ifndef DOXYGEN_SHOULD_SKIP_THIS

public:
  typedef TextTagTable CppObjectType;
  typedef TextTagTable_Class CppClassType;
  typedef GtkTextTagTable BaseObjectType;
  typedef GtkTextTagTableClass BaseClassType;

private:  friend class TextTagTable_Class;
  static CppClassType texttagtable_class_;

private:
  // noncopyable
  TextTagTable(const TextTagTable&);
  TextTagTable& operator=(const TextTagTable&);

protected:
  explicit TextTagTable(const Glib::ConstructParams& construct_params);
  explicit TextTagTable(GtkTextTagTable* castitem);

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

public:
  virtual ~TextTagTable();

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  static GType get_type()      G_GNUC_CONST;
  static GType get_base_type() G_GNUC_CONST;
#endif

  ///Provides access to the underlying C GObject.
  GtkTextTagTable*       gobj()       { return reinterpret_cast<GtkTextTagTable*>(gobject_); }

  ///Provides access to the underlying C GObject.
  const GtkTextTagTable* gobj() const { return reinterpret_cast<GtkTextTagTable*>(gobject_); }

  ///Provides access to the underlying C instance. The caller is responsible for unrefing it. Use when directly setting fields in structs.
  GtkTextTagTable* gobj_copy();

private:

protected:

  TextTagTable();

public:
  
  static Glib::RefPtr<TextTagTable> create();


  /** Add a tag to the table. The tag is assigned the highest priority
   * in the table.
   * 
   *  @a tag  must not be in a tag table already, and may not have
   * the same name as an already-added tag.
   * @param tag A Gtk::TextTag.
   */
  void add(const Glib::RefPtr<TextTag>& tag);
  
  /** Remove a tag from the table. This will remove the table's
   * reference to the tag, so be careful - the tag will end
   * up destroyed if you don't have a reference to it.
   * @param tag A Gtk::TextTag.
   */
  void remove(const Glib::RefPtr<TextTag>& tag);
  
  /** Look up a named tag.
   * @param name Name of a tag.
   * @return The tag, or <tt>0</tt> if none by that name is in the table.
   */
  Glib::RefPtr<TextTag> lookup(const Glib::ustring& name);
  
  /** Look up a named tag.
   * @param name Name of a tag.
   * @return The tag, or <tt>0</tt> if none by that name is in the table.
   */
  Glib::RefPtr<const TextTag> lookup(const Glib::ustring& name) const;

  typedef sigc::slot<void, const Glib::RefPtr<TextTag>&> SlotForEach;
  void foreach(const SlotForEach& slot);
  

  /** Return value: number of tags in @a table 
   * @return Number of tags in @a table .
   */
  int get_size() const;

  
  /**
   * @par Prototype:
   * <tt>void on_my_%tag_changed(const Glib::RefPtr<TextTag>& tag, bool size_changed)</tt>
   */

  Glib::SignalProxy2< void,const Glib::RefPtr<TextTag>&,bool > signal_tag_changed();

  
  /**
   * @par Prototype:
   * <tt>void on_my_%tag_added(const Glib::RefPtr<TextTag>& tag)</tt>
   */

  Glib::SignalProxy1< void,const Glib::RefPtr<TextTag>& > signal_tag_added();

  
  /**
   * @par Prototype:
   * <tt>void on_my_%tag_removed(const Glib::RefPtr<TextTag>& tag)</tt>
   */

  Glib::SignalProxy1< void,const Glib::RefPtr<TextTag>& > signal_tag_removed();


public:

public:
  //C++ methods used to invoke GTK+ virtual functions:
#ifdef GLIBMM_VFUNCS_ENABLED
#endif //GLIBMM_VFUNCS_ENABLED

protected:
  //GTK+ Virtual Functions (override these to change behaviour):
#ifdef GLIBMM_VFUNCS_ENABLED
#endif //GLIBMM_VFUNCS_ENABLED

  //Default Signal Handlers::
#ifdef GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED
  virtual void on_tag_changed(const Glib::RefPtr<TextTag>& tag, bool size_changed);
  virtual void on_tag_added(const Glib::RefPtr<TextTag>& tag);
  virtual void on_tag_removed(const Glib::RefPtr<TextTag>& tag);
#endif //GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED


};

} /* namespace Gtk */


namespace Glib
{
  /** A Glib::wrap() method for this object.
   * 
   * @param object The C instance.
   * @param take_copy False if the result should take ownership of the C instance. True if it should take a new copy or ref.
   * @result A C++ instance that wraps this C instance.
   *
   * @relates Gtk::TextTagTable
   */
  Glib::RefPtr<Gtk::TextTagTable> wrap(GtkTextTagTable* object, bool take_copy = false);
}


#endif /* _GTKMM_TEXTTAGTABLE_H */

