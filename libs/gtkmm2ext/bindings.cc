#include <iostream>
#include "pbd/xml++.h"
#include "gtkmm2ext/bindings.h"
#include "gtkmm2ext/keyboard.h"

#include "i18n.h"

using namespace std;
using namespace Glib;
using namespace Gtk;
using namespace Gtkmm2ext;

uint32_t KeyboardKey::_ignored_state = 0;

KeyboardKey::KeyboardKey (uint32_t state, uint32_t keycode)
{
        uint32_t ignore = _ignored_state;
        
        if (gdk_keyval_is_upper (keycode) && gdk_keyval_is_lower (keycode)) {
                /* key is not subject to case, so ignore SHIFT
                 */
                ignore |= GDK_SHIFT_MASK;
        }

        _val = (state & ~ignore);
        _val <<= 32;
        _val |= keycode;
};


string
KeyboardKey::name () const
{
        int s = state();
        
        string str;

        if (s & Keyboard::PrimaryModifier) {
                str += "Primary";
        } 
        if (s & Keyboard::SecondaryModifier) {
                if (!str.empty()) {
                        str += '-';
                }
                str += "Secondary";
        }
        if (s & Keyboard::TertiaryModifier) {
                if (!str.empty()) {
                        str += '-';
                }
                str += "Tertiary";
        } 
        if (s & Keyboard::Level4Modifier) {
                if (!str.empty()) {
                        str += '-';
                }
                str += "Level4";
        }
        
        if (!str.empty()) {
                str += '-';
        }

        str += gdk_keyval_name (key());

        return str;
}

bool
KeyboardKey::make_key (const string& str, KeyboardKey& k)
{
        int s = 0;

        if (str.find ("Primary") != string::npos) {
                s |= Keyboard::PrimaryModifier;
        }

        if (str.find ("Secondary") != string::npos) {
                s |= Keyboard::SecondaryModifier;
        }

        if (str.find ("Tertiary") != string::npos) {
                s |= Keyboard::TertiaryModifier;
        }

        if (str.find ("Level4") != string::npos) {
                s |= Keyboard::Level4Modifier;
        }

        string::size_type lastmod = str.find_last_of ('-');
        guint keyval;

        if (lastmod == string::npos) {
                keyval = gdk_keyval_from_name (str.c_str());
        } else {
                keyval = gdk_keyval_from_name (str.substr (lastmod+1).c_str());
        }

        if (keyval == GDK_VoidSymbol) {
                return false;
        }

        k = KeyboardKey (s, keyval);
        return true;
}

Bindings::Bindings ()
        : action_map (0)
{
}

Bindings::~Bindings()
{
}

void
Bindings::set_action_map (ActionMap& am)
{
        action_map = &am;
        press_bindings.clear ();
        release_bindings.clear ();
}

bool
Bindings::activate (KeyboardKey kb, KeyboardKey::Operation op)
{
        KeybindingMap* kbm;

        switch (op) {
        case KeyboardKey::Press:
                kbm = &press_bindings;
                break;
        case KeyboardKey::Release:
                kbm = &release_bindings;
                break;
        }

        KeybindingMap::iterator k = kbm->find (kb);

        if (k == kbm->end()) {
                /* no entry for this key in the state map */
                return false;
        }

        /* lets do it ... */

        k->second->activate ();
        return true;
}

void
Bindings::add (KeyboardKey kb, KeyboardKey::Operation op, RefPtr<Action> what)
{
        KeybindingMap* kbm;

        switch (op) {
        case KeyboardKey::Press:
                kbm = &press_bindings;
                break;
        case KeyboardKey::Release:
                kbm = &release_bindings;
                break;
        }

        KeybindingMap::iterator k = kbm->find (kb);

        if (k == kbm->end()) {
                pair<KeyboardKey,RefPtr<Action> > newpair (kb, what);
                kbm->insert (newpair);
                cerr << "Bindings added " << kb.key() << " w/ " << kb.state() << endl;
        } else {
                k->second = what;
        }
}

void
Bindings::remove (KeyboardKey kb, KeyboardKey::Operation op)
{
        KeybindingMap* kbm;

        switch (op) {
        case KeyboardKey::Press:
                kbm = &press_bindings;
                break;
        case KeyboardKey::Release:
                kbm = &release_bindings;
                break;
        }

        KeybindingMap::iterator k = kbm->find (kb);

        if (k != kbm->end()) {
                kbm->erase (k);
        }
}

bool
Bindings::save (const string& path)
{
        XMLTree tree;
        XMLNode* root = new XMLNode (X_("Bindings"));
        tree.set_root (root);

        XMLNode* presses = new XMLNode (X_("Press"));
        root->add_child_nocopy (*presses);

        for (KeybindingMap::iterator k = press_bindings.begin(); k != press_bindings.end(); ++k) {
                XMLNode* child;
                child = new XMLNode (X_("Binding"));
                child->add_property (X_("key"), k->first.name());
                child->add_property (X_("action"), k->second->get_name());
                presses->add_child_nocopy (*child);
        }

        XMLNode* releases = new XMLNode (X_("Release"));
        root->add_child_nocopy (*releases);

        for (KeybindingMap::iterator k = release_bindings.begin(); k != release_bindings.end(); ++k) {
                XMLNode* child;
                child = new XMLNode (X_("Binding"));
                child->add_property (X_("key"), k->first.name());
                child->add_property (X_("action"), k->second->get_name());
                releases->add_child_nocopy (*child);
        }

        if (!tree.write (path)) {
                ::unlink (path.c_str());
                return false;
        }

        return true;
}

bool
Bindings::load (const string& path)
{
        XMLTree tree;

        if (!action_map) {
                return false;
        }

        if (!tree.read (path)) {
                return false;
        }
        
        press_bindings.clear ();
        release_bindings.clear ();

        XMLNode& root (*tree.root());
        const XMLNodeList& children (root.children());

        for (XMLNodeList::const_iterator i = children.begin(); i != children.end(); ++i) {

                if ((*i)->name() == X_("Press") || (*i)->name() == X_("Release")) {

                        KeyboardKey::Operation op;

                        if ((*i)->name() == X_("Press")) {
                                op = KeyboardKey::Press;
                        } else {
                                op = KeyboardKey::Release;
                        }
                        
                        const XMLNodeList& gchildren ((*i)->children());

                        for (XMLNodeList::const_iterator p = gchildren.begin(); p != gchildren.end(); ++p) {

                                XMLProperty* ap;
                                XMLProperty* kp;

                                ap = (*p)->property ("action");
                                kp = (*p)->property ("key");

                                if (!ap || !kp) {
                                        continue;
                                }

                                RefPtr<Action> act = action_map->find_action (ap->value());
                                
                                if (!act) {
                                        continue;
                                }

                                KeyboardKey k;
                                
                                if (!KeyboardKey::make_key (kp->value(), k)) {
                                        continue;
                                }

                                add (k, op, act);
                        }
                }
        }

        return true;
}

RefPtr<Action>
ActionMap::find_action (const string& name)
{
        _ActionMap::iterator a = actions.find (name);

        if (a != actions.end()) {
                return a->second;
        }

        return RefPtr<Action>();
}

RefPtr<Action> 
ActionMap::register_action (const char* path,
                            const char* name, const char* label, sigc::slot<void> sl)
{
        string fullpath;

        RefPtr<Action> act = Action::create (name, label);

        act->signal_activate().connect (sl);

        fullpath = path;
        fullpath += '/';
        fullpath += name;

        actions.insert (_ActionMap::value_type (fullpath, act));
        return act;
}

RefPtr<Action> 
ActionMap::register_radio_action (const char* path, Gtk::RadioAction::Group& rgroup,
                                  const char* name, const char* label, 
                                  sigc::slot<void,GtkAction*> sl,
                                  int value)
{
        string fullpath;

        RefPtr<Action> act = RadioAction::create (rgroup, name, label);
        RefPtr<RadioAction> ract = RefPtr<RadioAction>::cast_dynamic(act);
        ract->property_value() = value;

        act->signal_activate().connect (sigc::bind (sl, act->gobj()));

        fullpath = path;
        fullpath += '/';
        fullpath += name;

        actions.insert (_ActionMap::value_type (fullpath, act));
        return act;
}

RefPtr<Action> 
ActionMap::register_toggle_action (const char* path,
                                   const char* name, const char* label, sigc::slot<void> sl)
{
        string fullpath;

        RefPtr<Action> act = ToggleAction::create (name, label);

        act->signal_activate().connect (sl);

        fullpath = path;
        fullpath += '/';
        fullpath += name;

        actions.insert (_ActionMap::value_type (fullpath, act));
        return act;
}
