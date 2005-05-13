// Generated by gtkmmproc -- DO NOT MODIFY!

#include <gtkmm/box.h>
#include <gtkmm/private/box_p.h>

// -*- c++ -*-
/* $Id$ */

/* 
 *
 * Copyright 1998-2002 The gtkmm Development Team
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

#include <gtk/gtkbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <glibmm/wrap.h>

namespace Gtk
{

namespace Box_Helpers
{

Widget* Child::get_widget() const
{ 
  return Glib::wrap(gobj()->widget);
}

void Child::set_options(PackOptions options, guint padding)
{
  const bool expand = (options == PACK_EXPAND_PADDING || options == PACK_EXPAND_WIDGET);
  const bool fill   = (options == PACK_EXPAND_WIDGET);

  set_options(expand, fill, padding);
}

void Child::set_options(bool expand, bool fill, guint padding)
{
  gobj()->expand  = expand;
  gobj()->fill    = fill;
  gobj()->padding = padding;
}

void Child::set_pack(PackType pack)
{
  gobj()->pack = pack;
}


/**************************************************************************/


typedef Box_Helpers::BoxList::iterator box_iterator;

box_iterator BoxList::insert(box_iterator position, const Element& e)
{
  iterator i;
  bool expand = (e.options_ == PACK_EXPAND_PADDING) || (e.options_ == PACK_EXPAND_WIDGET);
  bool fill = (e.options_ == PACK_EXPAND_WIDGET);

  if (e.pack_ == PACK_START)
    gtk_box_pack_start(gparent(), (e.widget_? e.widget_->gobj() : 0),
                       (gboolean)expand, (gboolean)fill, e.padding_);
  else
    gtk_box_pack_end(gparent(), (e.widget_ ? e.widget_->gobj() : 0),
                       (gboolean)expand, (gboolean)fill, e.padding_);

  i = --end();

  if (position!=end())
    reorder(i, position);

  return i;
}

// Non-standard
void BoxList::reorder(box_iterator loc, box_iterator pos)
{
  int position = g_list_position(glist(), pos.node_);
  gtk_box_reorder_child(gparent(), loc->gobj()->widget, position);
}

} /* namespace Box_Helpers */

Box::BoxList& Box::children()
{
  children_proxy_ = BoxList(gobj());
  return children_proxy_;
}

const Box::BoxList& Box::children() const
{
  children_proxy_ = BoxList(const_cast<GtkBox*>(gobj()));
  return children_proxy_;
}

void Box::pack_start(Widget& child, PackOptions options, guint padding)
{
  bool expand = (options == PACK_EXPAND_PADDING) || (options == PACK_EXPAND_WIDGET);
  bool fill = (options == PACK_EXPAND_WIDGET);

  gtk_box_pack_start(gobj(), child.gobj(), (gboolean)expand, (gboolean)fill, padding);
}

void Box::pack_end(Widget& child, PackOptions options, guint padding)
{
  bool expand = (options == PACK_EXPAND_PADDING) || (options == PACK_EXPAND_WIDGET);
  bool fill = (options == PACK_EXPAND_WIDGET);

  gtk_box_pack_end(gobj(), child.gobj(), (gboolean)expand, (gboolean)fill, padding);
}


} /* namespace Gtk */


namespace
{
} // anonymous namespace


namespace Glib
{

Gtk::Box* wrap(GtkBox* object, bool take_copy)
{
  return dynamic_cast<Gtk::Box *> (Glib::wrap_auto ((GObject*)(object), take_copy));
}

} /* namespace Glib */

namespace Gtk
{


/* The *_Class implementation: */

const Glib::Class& Box_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Class has to know the class init function to clone custom types.
    class_init_func_ = &Box_Class::class_init_function;

    // This is actually just optimized away, apparently with no harm.
    // Make sure that the parent type has been created.
    //CppClassParent::CppObjectType::get_type();

    // Create the wrapper type, with the same class/instance size as the base type.
    register_derived_type(gtk_box_get_type());

    // Add derived versions of interfaces, if the C type implements any interfaces:
  }

  return *this;
}

void Box_Class::class_init_function(void* g_class, void* class_data)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_class);
  CppClassParent::class_init_function(klass, class_data);

}


Glib::ObjectBase* Box_Class::wrap_new(GObject* o)
{
  return manage(new Box((GtkBox*)(o)));

}


/* The implementation: */

Box::Box(const Glib::ConstructParams& construct_params)
:
  Gtk::Container(construct_params)
{
  }

Box::Box(GtkBox* castitem)
:
  Gtk::Container((GtkContainer*)(castitem))
{
  }

Box::~Box()
{
  destroy_();
}

Box::CppClassType Box::box_class_; // initialize static member

GType Box::get_type()
{
  return box_class_.init().get_type();
}

GType Box::get_base_type()
{
  return gtk_box_get_type();
}


namespace Box_Helpers
{

BoxList::iterator BoxList::find(const_reference w)
{
  iterator i = begin();
  for(i = begin(); i != end() && (i->get_widget()->gobj() != w.get_widget()->gobj()); i++);
  return i;
}

BoxList::iterator BoxList::find(Widget& w)
{
  iterator i;
  for(i = begin(); i != end() && ((GtkWidget*)i->get_widget()->gobj() != w.gobj()); i++);
  return i;
}

} /* namespace Box_Helpers */


namespace Box_Helpers
{

void BoxList::remove(const_reference child)
{
  gtk_container_remove(GTK_CONTAINER(gparent_),
                       (GtkWidget*)(child.get_widget()->gobj()));
}

void BoxList::remove(Widget& widget)
{
  gtk_container_remove(GTK_CONTAINER(gparent_), (GtkWidget*)(widget.gobj()));
}

BoxList::iterator BoxList::erase(iterator position)
{
  //Check that it is a valid iterator, to a real item:
  if ( !position.node_|| (position == end()) )
    return end();

  //Get an iterator the the next item, to return:
  iterator next = position;
  next++;

  //Use GTK+ C function to remove it, by providing the GtkWidget*:
  gtk_container_remove( GTK_CONTAINER(gparent_), (GtkWidget*)(position->get_widget()->gobj()) );
  return next;
}

} /* namespace Box_Helpers */


namespace Box_Helpers
{

BoxList::BoxList()
{}

BoxList::BoxList(GtkBox* gparent)
: type_base((GObject*)gparent)
{}

BoxList::BoxList(const BoxList& src)
:
  type_base(src)
{}

BoxList& BoxList::operator=(const BoxList& src)
{
  type_base::operator=(src);
  return *this;
}

GList*& BoxList::glist() const
{
  return ((GtkBox*)gparent_)->children;
}

void BoxList::erase(iterator start, iterator stop)
{
  type_base::erase(start, stop);
}

GtkBox* BoxList::gparent()
{
  return (GtkBox*)type_base::gparent();
}

const GtkBox* BoxList::gparent() const
{
  return (GtkBox*)type_base::gparent();
}

BoxList::reference BoxList::operator[](size_type l) const
{
  return type_base::operator[](l);
}

} /* namespace Box_Helpers */

Box::Box()
:
  Glib::ObjectBase(0), //Mark this class as gtkmmproc-generated, rather than a custom class, to allow vfunc optimisations.
  Gtk::Container(Glib::ConstructParams(box_class_.init()))
{
  }

void Box::pack_start(Widget& child, bool expand, bool fill, guint padding)
{
  gtk_box_pack_start(gobj(), (child).gobj(), static_cast<int>(expand), static_cast<int>(fill), padding);
}

void Box::pack_end(Widget& child, bool expand, bool fill, guint padding)
{
  gtk_box_pack_end(gobj(), (child).gobj(), static_cast<int>(expand), static_cast<int>(fill), padding);
}

void Box::set_homogeneous(bool homogeneous)
{
  gtk_box_set_homogeneous(gobj(), static_cast<int>(homogeneous));
}

bool Box::get_homogeneous() const
{
  return gtk_box_get_homogeneous(const_cast<GtkBox*>(gobj()));
}

void Box::set_spacing(int spacing)
{
  gtk_box_set_spacing(gobj(), spacing);
}

int Box::get_spacing() const
{
  return gtk_box_get_spacing(const_cast<GtkBox*>(gobj()));
}

void Box::reorder_child(Widget& child, int pos)
{
  gtk_box_reorder_child(gobj(), (child).gobj(), pos);
}


Glib::PropertyProxy<int> Box::property_spacing() 
{
  return Glib::PropertyProxy<int>(this, "spacing");
}

Glib::PropertyProxy_ReadOnly<int> Box::property_spacing() const
{
  return Glib::PropertyProxy_ReadOnly<int>(this, "spacing");
}

Glib::PropertyProxy<bool> Box::property_homogeneous() 
{
  return Glib::PropertyProxy<bool>(this, "homogeneous");
}

Glib::PropertyProxy_ReadOnly<bool> Box::property_homogeneous() const
{
  return Glib::PropertyProxy_ReadOnly<bool>(this, "homogeneous");
}


} // namespace Gtk


namespace Glib
{

Gtk::VBox* wrap(GtkVBox* object, bool take_copy)
{
  return dynamic_cast<Gtk::VBox *> (Glib::wrap_auto ((GObject*)(object), take_copy));
}

} /* namespace Glib */

namespace Gtk
{


/* The *_Class implementation: */

const Glib::Class& VBox_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Class has to know the class init function to clone custom types.
    class_init_func_ = &VBox_Class::class_init_function;

    // This is actually just optimized away, apparently with no harm.
    // Make sure that the parent type has been created.
    //CppClassParent::CppObjectType::get_type();

    // Create the wrapper type, with the same class/instance size as the base type.
    register_derived_type(gtk_vbox_get_type());

    // Add derived versions of interfaces, if the C type implements any interfaces:
  }

  return *this;
}

void VBox_Class::class_init_function(void* g_class, void* class_data)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_class);
  CppClassParent::class_init_function(klass, class_data);

}


Glib::ObjectBase* VBox_Class::wrap_new(GObject* o)
{
  return manage(new VBox((GtkVBox*)(o)));

}


/* The implementation: */

VBox::VBox(const Glib::ConstructParams& construct_params)
:
  Gtk::Box(construct_params)
{
  }

VBox::VBox(GtkVBox* castitem)
:
  Gtk::Box((GtkBox*)(castitem))
{
  }

VBox::~VBox()
{
  destroy_();
}

VBox::CppClassType VBox::vbox_class_; // initialize static member

GType VBox::get_type()
{
  return vbox_class_.init().get_type();
}

GType VBox::get_base_type()
{
  return gtk_vbox_get_type();
}

VBox::VBox(bool homogeneous, int spacing)
:
  Glib::ObjectBase(0), //Mark this class as gtkmmproc-generated, rather than a custom class, to allow vfunc optimisations.
  Gtk::Box(Glib::ConstructParams(vbox_class_.init(), "homogeneous", static_cast<int>(homogeneous), "spacing", spacing, (char*) 0))
{
  }


} // namespace Gtk


namespace Glib
{

Gtk::HBox* wrap(GtkHBox* object, bool take_copy)
{
  return dynamic_cast<Gtk::HBox *> (Glib::wrap_auto ((GObject*)(object), take_copy));
}

} /* namespace Glib */

namespace Gtk
{


/* The *_Class implementation: */

const Glib::Class& HBox_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Class has to know the class init function to clone custom types.
    class_init_func_ = &HBox_Class::class_init_function;

    // This is actually just optimized away, apparently with no harm.
    // Make sure that the parent type has been created.
    //CppClassParent::CppObjectType::get_type();

    // Create the wrapper type, with the same class/instance size as the base type.
    register_derived_type(gtk_hbox_get_type());

    // Add derived versions of interfaces, if the C type implements any interfaces:
  }

  return *this;
}

void HBox_Class::class_init_function(void* g_class, void* class_data)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_class);
  CppClassParent::class_init_function(klass, class_data);

}


Glib::ObjectBase* HBox_Class::wrap_new(GObject* o)
{
  return manage(new HBox((GtkHBox*)(o)));

}


/* The implementation: */

HBox::HBox(const Glib::ConstructParams& construct_params)
:
  Gtk::Box(construct_params)
{
  }

HBox::HBox(GtkHBox* castitem)
:
  Gtk::Box((GtkBox*)(castitem))
{
  }

HBox::~HBox()
{
  destroy_();
}

HBox::CppClassType HBox::hbox_class_; // initialize static member

GType HBox::get_type()
{
  return hbox_class_.init().get_type();
}

GType HBox::get_base_type()
{
  return gtk_hbox_get_type();
}

HBox::HBox(bool homogeneous, int spacing)
:
  Glib::ObjectBase(0), //Mark this class as gtkmmproc-generated, rather than a custom class, to allow vfunc optimisations.
  Gtk::Box(Glib::ConstructParams(hbox_class_.init(), "homogeneous", static_cast<int>(homogeneous), "spacing", spacing, (char*) 0))
{
  }


} // namespace Gtk


