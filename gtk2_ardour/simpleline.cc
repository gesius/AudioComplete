// Generated by gtkmmproc -- DO NOT MODIFY!

#include "simpleline.h"
#include "simpleline_p.h"

/* $Id$ */

/* line.ccg
 *
 * Copyright (C) 1998 EMC Capital Management Inc.
 * Developed by Havoc Pennington <hp@pobox.com>
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

//#include <libgnomecanvasmm/group.h>

namespace Gnome
{

namespace Canvas
{

SimpleLine::SimpleLine(Group& parentx)
  : Item(GNOME_CANVAS_ITEM(g_object_new(get_type(),0)))
{
  item_construct(parentx);
}

	SimpleLine::SimpleLine(Group& parentx, double x1, double y1, double x2, double y2)
  : Item(GNOME_CANVAS_ITEM(g_object_new(get_type(),0)))
{
  item_construct(parentx);
  set ("x1", x1, "y1", y1, "x2", x2, "y2", y2, 0);
}

} /* namespace Canvas */
} /* namespace Gnome */


namespace Glib
{

Gnome::Canvas::SimpleLine* wrap(GnomeCanvasSimpleLine* object, bool take_copy)
{
  return dynamic_cast<Gnome::Canvas::SimpleLine *> (Glib::wrap_auto ((GObject*)(object), take_copy));
}

} /* namespace Glib */

namespace Gnome
{

namespace Canvas
{


/* The *_Class implementation: */

const Glib::Class& SimpleLine_Class::init()
{
  if(!gtype_) // create the GType if necessary
  {
    // Glib::Class has to know the class init function to clone custom types.
    class_init_func_ = &SimpleLine_Class::class_init_function;

    // This is actually just optimized away, apparently with no harm.
    // Make sure that the parent type has been created.
    //CppClassParent::CppObjectType::get_type();

    // Create the wrapper type, with the same class/instance size as the base type.
    register_derived_type(gnome_canvas_simpleline_get_type());

    // Add derived versions of interfaces, if the C type implements any interfaces:
  }

  return *this;
}

void SimpleLine_Class::class_init_function(void* g_class, void* class_data)
{
  BaseClassType *const klass = static_cast<BaseClassType*>(g_class);
  CppClassParent::class_init_function(klass, class_data);

}


Glib::ObjectBase* SimpleLine_Class::wrap_new(GObject* o)
{
  return manage(new SimpleLine((GnomeCanvasSimpleLine*)(o)));

}


/* The implementation: */

SimpleLine::SimpleLine(const Glib::ConstructParams& construct_params)
:
  Item(construct_params)
{
  }

SimpleLine::SimpleLine(GnomeCanvasSimpleLine* castitem)
:
  Item((GnomeCanvasItem*)(castitem))
{
  }

SimpleLine::~SimpleLine()
{
  destroy_();
}

SimpleLine::CppClassType SimpleLine::line_class_; // initialize static member

GType SimpleLine::get_type()
{
  return line_class_.init().get_type();
}

GType SimpleLine::get_base_type()
{
  return gnome_canvas_line_get_type();
}

Glib::PropertyProxy<guint> SimpleLine::property_color_rgba() 
{
  return Glib::PropertyProxy<guint>(this, "color-rgba");
}

Glib::PropertyProxy_ReadOnly<guint> SimpleLine::property_color_rgba() const
{
  return Glib::PropertyProxy_ReadOnly<guint>(this, "color-rgba");
}

Glib::PropertyProxy<double> SimpleLine::property_x1() 
{
  return Glib::PropertyProxy<double>(this, "x1");
}

Glib::PropertyProxy_ReadOnly<double> SimpleLine::property_x1() const
{
  return Glib::PropertyProxy_ReadOnly<double>(this, "x1");
}

Glib::PropertyProxy<double> SimpleLine::property_x2() 
{
  return Glib::PropertyProxy<double>(this, "x2");
}

Glib::PropertyProxy_ReadOnly<double> SimpleLine::property_x2() const
{
  return Glib::PropertyProxy_ReadOnly<double>(this, "x2");
}

Glib::PropertyProxy<double> SimpleLine::property_y1() 
{
  return Glib::PropertyProxy<double>(this, "y1");
}

Glib::PropertyProxy_ReadOnly<double> SimpleLine::property_y1() const
{
  return Glib::PropertyProxy_ReadOnly<double>(this, "y1");
}

Glib::PropertyProxy<double> SimpleLine::property_y2() 
{
  return Glib::PropertyProxy<double>(this, "y2");
}

Glib::PropertyProxy_ReadOnly<double> SimpleLine::property_y2() const
{
  return Glib::PropertyProxy_ReadOnly<double>(this, "y2");
}

} // namespace Canvas

} // namespace Gnome


