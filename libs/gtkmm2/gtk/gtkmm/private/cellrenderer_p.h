// -*- c++ -*-
// Generated by gtkmmproc -- DO NOT MODIFY!
#ifndef _GTKMM_CELLRENDERER_P_H
#define _GTKMM_CELLRENDERER_P_H
#include <gtkmm/private/object_p.h>
#include <gtk/gtkcellrenderer.h>

#include <glibmm/class.h>

namespace Gtk
{

class CellRenderer_Class : public Glib::Class
{
public:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  typedef CellRenderer CppObjectType;
  typedef GtkCellRenderer BaseObjectType;
  typedef GtkCellRendererClass BaseClassType;
  typedef Gtk::Object_Class CppClassParent;
  typedef GtkObjectClass BaseClassParent;

  friend class CellRenderer;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

  const Glib::Class& init();

  static void class_init_function(void* g_class, void* class_data);

  static Glib::ObjectBase* wrap_new(GObject*);

protected:

  //Callbacks (default signal handlers):
  //These will call the *_impl member methods, which will then call the existing default signal callbacks, if any.
  //You could prevent the original default signal handlers being called by overriding the *_impl method.
  static void editing_canceled_callback(GtkCellRenderer* self);

  //Callbacks (virtual functions):
  static void get_size_vfunc_callback(GtkCellRenderer* self, GtkWidget* widget, GdkRectangle* cell_area, gint* x_offset, gint* y_offset, gint* width, gint* height);
  static void render_vfunc_callback(GtkCellRenderer* self, GdkDrawable* window, GtkWidget* widget, GdkRectangle* background_area, GdkRectangle* cell_area, GdkRectangle* expose_area, GtkCellRendererState flags);
  static gboolean activate_vfunc_callback(GtkCellRenderer* self, GdkEvent* event, GtkWidget* widget, const gchar* path, GdkRectangle* background_area, GdkRectangle* cell_area, GtkCellRendererState flags);
  static GtkCellEditable* start_editing_vfunc_callback(GtkCellRenderer* self, GdkEvent* event, GtkWidget* widget, const gchar* path, GdkRectangle* background_area, GdkRectangle* cell_area, GtkCellRendererState flags);
};


} // namespace Gtk

#endif /* _GTKMM_CELLRENDERER_P_H */

