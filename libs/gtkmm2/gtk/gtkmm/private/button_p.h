// -*- c++ -*-
// Generated by gtkmmproc -- DO NOT MODIFY!
#ifndef _GTKMM_BUTTON_P_H
#define _GTKMM_BUTTON_P_H
#include <gtkmm/private/bin_p.h>

#include <glibmm/class.h>

namespace Gtk
{

class Button_Class : public Glib::Class
{
public:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  typedef Button CppObjectType;
  typedef GtkButton BaseObjectType;
  typedef GtkButtonClass BaseClassType;
  typedef Gtk::Bin_Class CppClassParent;
  typedef GtkBinClass BaseClassParent;

  friend class Button;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

  const Glib::Class& init();

  static void class_init_function(void* g_class, void* class_data);

  static Glib::ObjectBase* wrap_new(GObject*);

protected:

  //Callbacks (default signal handlers):
  //These will call the *_impl member methods, which will then call the existing default signal callbacks, if any.
  //You could prevent the original default signal handlers being called by overriding the *_impl method.
  static void pressed_callback(GtkButton* self);
  static void released_callback(GtkButton* self);
  static void clicked_callback(GtkButton* self);
  static void enter_callback(GtkButton* self);
  static void leave_callback(GtkButton* self);
  static void activate_callback(GtkButton* self);

  //Callbacks (virtual functions):
};


} // namespace Gtk

#endif /* _GTKMM_BUTTON_P_H */

