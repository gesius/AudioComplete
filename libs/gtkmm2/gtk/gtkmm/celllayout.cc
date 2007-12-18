// Generated by gtkmmproc -- DO NOT MODIFY!


#include <gtkmm/celllayout.h>
#include <gtkmm/private/celllayout_p.h>

// -*- c++ -*-
/* $Id$ */

/* Copyright 2003 The gtkmm Development Team
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

#include <gtk/gtkcelllayout.h>


static void SignalProxy_CellData_gtk_callback(GtkCellLayout* /* cell_layout */, GtkCellRenderer* /* cell */, GtkTreeModel* tree_model, GtkTreeIter* iter, gpointer data)
{
  Gtk::CellLayout::SlotCellData* the_slot = static_cast<Gtk::CellLayout::SlotCellData*>(data);

  #ifdef GLIBMM_EXCEPTIONS_ENABLED
  try
  {
  #endif //GLIBMM_EXCEPTIONS_ENABLED
    //We ignore the cell, because that was given as an argument to the connecting method, so the caller should know which one it is already.
    //And we ignore the tree_model because that can be obtained from the iter or from the CellLayout itself.
    (*the_slot)(Gtk::TreeModel::const_iterator(tree_model, iter));
  #ifdef GLIBMM_EXCEPTIONS_ENABLED
  }
  catch(...)
  {
    Glib::exception_handlers_invoke();
  }
  #endif //GLIBMM_EXCEPTIONS_ENABLED
}

static void SignalProxy_CellData_gtk_callback_destroy(void* data)
{
  delete static_cast<Gtk::CellLayout::SlotCellData*>(data);
}

namespace Gtk
{

#ifdef GLIBMM_PROPERTIES_ENABLED
void CellLayout::add_attribute(const Glib::PropertyProxy_Base& property, const TreeModelColumnBase& column)
{
  gtk_cell_layout_add_attribute(gobj(),
      (GtkCellRenderer*) property.get_object()->gobj(), property.get_name(), column.index());
}
#endif //GLIBMM_PROPERTIES_ENABLED

void CellLayout::add_attribute(CellRenderer& cell, const Glib::ustring& attribute, const TreeModelColumnBase& column)
{
  gtk_cell_layout_add_attribute(gobj(),
      (GtkCellRenderer*) cell.gobj(), attribute.c_str(), column.index());
}

void CellLayout::set_cell_data_func(CellRenderer& cell, const SlotCellData& slot)
{
  // Create a copy of the slot object.  A pointer to this will be passed
  // through the callback's data parameter.  It will be deleted
  // when SignalProxy_CellData_gtk_callback_destroy() is called.
  SlotCellData* slot_copy = new SlotCellData(slot);

  gtk_cell_layout_set_cell_data_func(gobj(), cell.gobj(),
      &SignalProxy_CellData_gtk_callback, slot_copy,
      &SignalProxy_CellData_gtk_callback_destroy);
}
  

} //namespace Gtk


namespace
{
} // anonymous namespace


namespace Glib
{

Glib::RefPtr<Gtk::CellLayout> wrap(GtkCellLayout* object, bool take_copy)
{
  return Glib::RefPtr<Gtk::CellLayout>( dynamic_cast<Gtk::CellLayout*> (Glib::wrap_auto_interface<Gtk::CellLayout> ((GObject*)(object), take_copy)) );
  //We use dynamic_cast<> in case of multiple inheritance.
}

} // namespace Glib


namespace Gtk
{


/* The *_Class implementation: */

const Glib::Interface_Class& CellLayout_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Interface_Class has to know the interface init function
    // in order to add interfaces to implementing types.
    class_init_func_ = &CellLayout_Class::iface_init_function;

    // We can not derive from another interface, and it is not necessary anyway.
    gtype_ = gtk_cell_layout_get_type();
  }

  return *this;
}

void CellLayout_Class::iface_init_function(void* g_iface, void*)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_iface);

  //This is just to avoid an "unused variable" warning when there are no vfuncs or signal handlers to connect.
  //This is a temporary fix until I find out why I can not seem to derive a GtkFileChooser interface. murrayc
  g_assert(klass != 0); 

#ifdef GLIBMM_VFUNCS_ENABLED
  klass->pack_start = &pack_start_vfunc_callback;
  klass->pack_end = &pack_end_vfunc_callback;
  klass->clear = &clear_vfunc_callback;
  klass->add_attribute = &add_attribute_vfunc_callback;
  klass->clear_attributes = &clear_attributes_vfunc_callback;
  klass->reorder = &reorder_vfunc_callback;
#endif //GLIBMM_VFUNCS_ENABLED

#ifdef GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED
#endif //GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED
}

#ifdef GLIBMM_VFUNCS_ENABLED
void CellLayout_Class::pack_start_vfunc_callback(GtkCellLayout* self, GtkCellRenderer* cell, gboolean expand)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->pack_start_vfunc(Glib::wrap(cell)
, expand
);
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->pack_start)
    (*base->pack_start)(self, cell, expand);

}
void CellLayout_Class::pack_end_vfunc_callback(GtkCellLayout* self, GtkCellRenderer* cell, gboolean expand)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->pack_end_vfunc(Glib::wrap(cell)
, expand
);
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->pack_end)
    (*base->pack_end)(self, cell, expand);

}
void CellLayout_Class::clear_vfunc_callback(GtkCellLayout* self)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->clear_vfunc();
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->clear)
    (*base->clear)(self);

}
void CellLayout_Class::add_attribute_vfunc_callback(GtkCellLayout* self, GtkCellRenderer* cell, const gchar* attribute, gint column)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->add_attribute_vfunc(Glib::wrap(cell)
, Glib::convert_const_gchar_ptr_to_ustring(attribute)
, column
);
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->add_attribute)
    (*base->add_attribute)(self, cell, attribute, column);

}
void CellLayout_Class::clear_attributes_vfunc_callback(GtkCellLayout* self, GtkCellRenderer* cell)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->clear_attributes_vfunc(Glib::wrap(cell)
);
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->clear_attributes)
    (*base->clear_attributes)(self, cell);

}
void CellLayout_Class::reorder_vfunc_callback(GtkCellLayout* self, GtkCellRenderer* cell, gint position)
{
  Glib::ObjectBase *const obj_base = static_cast<Glib::ObjectBase*>(
      Glib::ObjectBase::_get_current_wrapper((GObject*)self));

  // Non-gtkmmproc-generated custom classes implicitly call the default
  // Glib::ObjectBase constructor, which sets is_derived_. But gtkmmproc-
  // generated classes can use this optimisation, which avoids the unnecessary
  // parameter conversions if there is no possibility of the virtual function
  // being overridden:
  if(obj_base && obj_base->is_derived_())
  {
    CppObjectType *const obj = dynamic_cast<CppObjectType* const>(obj_base);
    if(obj) // This can be NULL during destruction.
    {
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      try // Trap C++ exceptions which would normally be lost because this is a C callback.
      {
      #endif //GLIBMM_EXCEPTIONS_ENABLED
        // Call the virtual member method, which derived classes might override.
        obj->reorder_vfunc(Glib::wrap(cell)
, position
);
        return;
      #ifdef GLIBMM_EXCEPTIONS_ENABLED
      }
      catch(...)
      {
        Glib::exception_handlers_invoke();
      }
      #endif //GLIBMM_EXCEPTIONS_ENABLED
    }
  }
  
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(self), CppObjectType::get_type()) // Get the interface.
)  );

  // Call the original underlying C function:
  if(base && base->reorder)
    (*base->reorder)(self, cell, position);

}
#endif //GLIBMM_VFUNCS_ENABLED

#ifdef GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED
#endif //GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED


Glib::ObjectBase* CellLayout_Class::wrap_new(GObject* object)
{
  return new CellLayout((GtkCellLayout*)(object));
}


/* The implementation: */

CellLayout::CellLayout()
:
  Glib::Interface(celllayout_class_.init())
{}

CellLayout::CellLayout(GtkCellLayout* castitem)
:
  Glib::Interface((GObject*)(castitem))
{}

CellLayout::~CellLayout()
{}

// static
void CellLayout::add_interface(GType gtype_implementer)
{
  celllayout_class_.init().add_interface(gtype_implementer);
}

CellLayout::CppClassType CellLayout::celllayout_class_; // initialize static member

GType CellLayout::get_type()
{
  return celllayout_class_.init().get_type();
}

GType CellLayout::get_base_type()
{
  return gtk_cell_layout_get_type();
}


void CellLayout::pack_start(CellRenderer& cell, bool expand)
{
gtk_cell_layout_pack_start(gobj(), (cell).gobj(), static_cast<int>(expand)); 
}

void CellLayout::pack_end(CellRenderer& cell, bool expand)
{
gtk_cell_layout_pack_end(gobj(), (cell).gobj(), static_cast<int>(expand)); 
}

Glib::ListHandle<CellRenderer*> CellLayout::get_cells()
{
  return Glib::ListHandle<CellRenderer*>(gtk_cell_layout_get_cells(gobj()), Glib::OWNERSHIP_SHALLOW);
}

Glib::ListHandle<const CellRenderer*> CellLayout::get_cells() const
{
  return Glib::ListHandle<const CellRenderer*>(gtk_cell_layout_get_cells(const_cast<GtkCellLayout*>(gobj())), Glib::OWNERSHIP_SHALLOW);
}

void CellLayout::clear()
{
gtk_cell_layout_clear(gobj()); 
}

void CellLayout::add_attribute(CellRenderer& cell, const Glib::ustring& attribute, int column)
{
gtk_cell_layout_add_attribute(gobj(), (cell).gobj(), attribute.c_str(), column); 
}

void CellLayout::clear_attributes(CellRenderer& cell)
{
gtk_cell_layout_clear_attributes(gobj(), (cell).gobj()); 
}

void CellLayout::reorder(CellRenderer& cell, int position)
{
gtk_cell_layout_reorder(gobj(), (cell).gobj(), position); 
}


#ifdef GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED
#endif //GLIBMM_DEFAULT_SIGNAL_HANDLERS_ENABLED

#ifdef GLIBMM_VFUNCS_ENABLED
void Gtk::CellLayout::pack_start_vfunc(CellRenderer* cell, bool expand) 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->pack_start)
    (*base->pack_start)(gobj(),(GtkCellRenderer*)Glib::unwrap(cell),static_cast<int>(expand));
}
void Gtk::CellLayout::pack_end_vfunc(CellRenderer* cell, bool expand) 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->pack_end)
    (*base->pack_end)(gobj(),(GtkCellRenderer*)Glib::unwrap(cell),static_cast<int>(expand));
}
void Gtk::CellLayout::clear_vfunc() 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->clear)
    (*base->clear)(gobj());
}
void Gtk::CellLayout::add_attribute_vfunc(CellRenderer* cell, const Glib::ustring& attribute, int column) 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->add_attribute)
    (*base->add_attribute)(gobj(),(GtkCellRenderer*)Glib::unwrap(cell),attribute.c_str(),column);
}
void Gtk::CellLayout::clear_attributes_vfunc(CellRenderer* cell) 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->clear_attributes)
    (*base->clear_attributes)(gobj(),(GtkCellRenderer*)Glib::unwrap(cell));
}
void Gtk::CellLayout::reorder_vfunc(CellRenderer* cell, int position) 
{
  BaseClassType *const base = static_cast<BaseClassType*>(
      g_type_interface_peek_parent( // Get the parent interface of the interface (The original underlying C interface).
g_type_interface_peek(G_OBJECT_GET_CLASS(gobject_), CppObjectType::get_type()) // Get the interface.
)  );

  if(base && base->reorder)
    (*base->reorder)(gobj(),(GtkCellRenderer*)Glib::unwrap(cell),position);
}
#endif //GLIBMM_VFUNCS_ENABLED


} // namespace Gtk


