/*
    Copyright (C) 2011 Paul Davis

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

#include "pbd/error.h"
#include "pbd/convert.h"

#include "ardour/automation_control.h"
#include "ardour/automation_list.h"
#include "ardour/pannable.h"
#include "ardour/pan_controllable.h"
#include "ardour/session.h"

using namespace PBD;
using namespace ARDOUR;

Pannable::Pannable (Session& s)
        : Automatable (s)
        , SessionHandleRef (s)
        , pan_azimuth_control (new PanControllable (s, "", this, PanAzimuthAutomation))
        , pan_elevation_control (new PanControllable (s, "", this, PanElevationAutomation))
        , pan_width_control (new PanControllable (s, "", this, PanWidthAutomation))
        , pan_frontback_control (new PanControllable (s, "", this, PanFrontBackAutomation))
        , pan_lfe_control (new PanControllable (s, "", this, PanLFEAutomation))
        , _auto_state (Off)
        , _auto_style (Absolute)
        , _has_state (false)
        , _responding_to_control_auto_state_change (0)
{
        add_control (pan_azimuth_control);
        add_control (pan_elevation_control);
        add_control (pan_width_control);
        add_control (pan_frontback_control);
        add_control (pan_lfe_control);

        /* all controls change state together */

        pan_azimuth_control->alist()->automation_state_changed.connect_same_thread (*this, boost::bind (&Pannable::control_auto_state_changed, this, _1));
        pan_elevation_control->alist()->automation_state_changed.connect_same_thread (*this, boost::bind (&Pannable::control_auto_state_changed, this, _1));
        pan_width_control->alist()->automation_state_changed.connect_same_thread (*this, boost::bind (&Pannable::control_auto_state_changed, this, _1));
        pan_frontback_control->alist()->automation_state_changed.connect_same_thread (*this, boost::bind (&Pannable::control_auto_state_changed, this, _1));
        pan_lfe_control->alist()->automation_state_changed.connect_same_thread (*this, boost::bind (&Pannable::control_auto_state_changed, this, _1));
}

void
Pannable::control_auto_state_changed (AutoState new_state)
{
        if (_responding_to_control_auto_state_change) {
                return;
        }

        _responding_to_control_auto_state_change++;

        pan_azimuth_control->set_automation_state (new_state);
        pan_width_control->set_automation_state (new_state);
        pan_elevation_control->set_automation_state (new_state);
        pan_frontback_control->set_automation_state (new_state);
        pan_lfe_control->set_automation_state (new_state);

        _responding_to_control_auto_state_change--;

        _auto_state = new_state;
        automation_state_changed (new_state);  /* EMIT SIGNAL */
}

void
Pannable::set_panner (boost::shared_ptr<Panner> p)
{
        _panner = p;
}

void
Pannable::set_automation_state (AutoState state)
{
        if (state != _auto_state) {
                _auto_state = state;

                const Controls& c (controls());
        
                for (Controls::const_iterator ci = c.begin(); ci != c.end(); ++ci) {
                        boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl>(ci->second);
                        if (ac) {
                                ac->alist()->set_automation_state (state);
                        }
                }
                
                session().set_dirty ();
                automation_state_changed (_auto_state);
        }
}

void
Pannable::set_automation_style (AutoStyle style)
{
        if (style != _auto_style) {
                _auto_style = style;

                const Controls& c (controls());
                
                for (Controls::const_iterator ci = c.begin(); ci != c.end(); ++ci) {
                        boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl>(ci->second);
                        if (ac) {
                                ac->alist()->set_automation_style (style);
                        }
                }
                
                session().set_dirty ();
                automation_style_changed ();
        }
}

void
Pannable::start_touch (double when)
{
        const Controls& c (controls());
        
        for (Controls::const_iterator ci = c.begin(); ci != c.end(); ++ci) {
                boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl>(ci->second);
                if (ac) {
                        ac->alist()->start_touch (when);
                }
        }
        g_atomic_int_set (&_touching, 1);
}

void
Pannable::stop_touch (bool mark, double when)
{
        const Controls& c (controls());
        
        for (Controls::const_iterator ci = c.begin(); ci != c.end(); ++ci) {
                boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl>(ci->second);
                if (ac) {
                        ac->alist()->stop_touch (mark, when);
                }
        }
        g_atomic_int_set (&_touching, 0);
}

XMLNode&
Pannable::get_state ()
{
        return state (true);
}

XMLNode&
Pannable::state (bool full)
{
        XMLNode* node = new XMLNode (X_("Pannable"));
        XMLNode* control_node;
        char buf[32];

        control_node = new XMLNode (X_("azimuth"));
        snprintf (buf, sizeof(buf), "%.12g", pan_azimuth_control->get_value());
        control_node->add_property (X_("value"), buf);
        node->add_child_nocopy (*control_node);
        
        control_node = new XMLNode (X_("width"));
        snprintf (buf, sizeof(buf), "%.12g", pan_width_control->get_value());
        control_node->add_property (X_("value"), buf);
        node->add_child_nocopy (*control_node);

        control_node = new XMLNode (X_("elevation"));
        snprintf (buf, sizeof(buf), "%.12g", pan_elevation_control->get_value());
        control_node->add_property (X_("value"), buf);
        node->add_child_nocopy (*control_node);

        control_node = new XMLNode (X_("frontback"));
        snprintf (buf, sizeof(buf), "%.12g", pan_frontback_control->get_value());
        control_node->add_property (X_("value"), buf);
        node->add_child_nocopy (*control_node);

        control_node = new XMLNode (X_("lfe"));
        snprintf (buf, sizeof(buf), "%.12g", pan_lfe_control->get_value());
        control_node->add_property (X_("value"), buf);
        node->add_child_nocopy (*control_node);
        
        node->add_child_nocopy (get_automation_xml_state ());

     return *node;
}

int
Pannable::set_state (const XMLNode& root, int /*version - not used*/)
{
        if (root.name() != X_("Pannable")) {
                warning << string_compose (_("Pannable given XML data for %1 - ignored"), root.name()) << endmsg;
                return -1;
        }
        
        XMLNodeList nlist;
	XMLNodeConstIterator niter;
	const XMLProperty *prop;

        nlist = root.children();

        for (niter = nlist.begin(); niter != nlist.end(); ++niter) {
                if ((*niter)->name() == X_("azimuth")) {
                        prop = (*niter)->property (X_("value"));
                        if (prop) {
                                pan_azimuth_control->set_value (atof (prop->value()));
                        }
                } else if ((*niter)->name() == X_("width")) {
                        prop = (*niter)->property (X_("value"));
                        if (prop) {
                                pan_width_control->set_value (atof (prop->value()));
                        }
                } else if ((*niter)->name() == X_("elevation")) {
                        prop = (*niter)->property (X_("value"));
                        if (prop) {
                                pan_elevation_control->set_value (atof (prop->value()));
                        }
                } else if ((*niter)->name() == X_("azimuth")) {
                        prop = (*niter)->property (X_("value"));
                        if (prop) {
                                pan_frontback_control->set_value (atof (prop->value()));
                        }
                } else if ((*niter)->name() == X_("lfe")) {
                        prop = (*niter)->property (X_("value"));
                        if (prop) {
                                pan_lfe_control->set_value (atof (prop->value()));
                        }
                } else if ((*niter)->name() == Automatable::xml_node_name) {
                        set_automation_xml_state (**niter, PanAzimuthAutomation);
                }
        }
        
        _has_state = true;

        return 0;
}



        
