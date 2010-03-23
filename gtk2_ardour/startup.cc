#include <fstream>
#include <algorithm>

#include <gtkmm/main.h>
#include <gtkmm/filechooser.h>

#include "pbd/failed_constructor.h"
#include "pbd/file_utils.h"
#include "pbd/filesystem.h"
#include "pbd/replace_all.h"

#include "ardour/filesystem_paths.h"
#include "ardour/recent_sessions.h"
#include "ardour/session.h"
#include "ardour/session_state_utils.h"
#include "ardour/template_utils.h"

#include "startup.h"
#include "opts.h"
#include "engine_dialog.h"
#include "i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gdk;
using namespace Glib;
using namespace PBD;
using namespace ARDOUR;

ArdourStartup* ArdourStartup::the_startup = 0;

static string poor_mans_glob (string path)
{
	string copy = path;
	replace_all (copy, "~", Glib::get_home_dir());
	return copy;
}


ArdourStartup::ArdourStartup ()
	: _response (RESPONSE_OK)
	, ic_new_session_button (_("Open a new session"))
	, ic_existing_session_button (_("Open an existing session"))
	, monitor_via_hardware_button (_("Use an external mixer or the hardware mixer of your audio interface.\n\
Ardour will play NO role in monitoring"))
	, monitor_via_ardour_button (string_compose (_("Ask %1 to playback material as it is being recorded"), PROGRAM_NAME))
        , use_monitor_section_button (_("Use a monitor bus in new sessions (more complex, more control)"))
        , no_monitor_section_button (_("Just use the master out bus (simpler, less control)"))
	, new_folder_chooser (FILE_CHOOSER_ACTION_SELECT_FOLDER)
	, more_new_session_options_button (_("I'd like more options for this session"))
	, _output_limit_count_adj (1, 0, 100, 1, 10, 0)
	, _input_limit_count_adj (1, 0, 100, 1, 10, 0)
	, _master_bus_channel_count_adj (2, 0, 100, 1, 10, 0)

{
	audio_page_index = -1;
	initial_choice_index = -1;
	new_user_page_index = -1;
	default_folder_page_index = -1;
	monitoring_page_index = -1;
	session_page_index = -1;
	final_page_index = -1;
	session_options_page_index = -1;
	new_only = false;

	engine_dialog = 0;
	config_modified = false;
	default_dir_chooser = 0;

	use_template_button.set_group (session_template_group);
	use_session_as_template_button.set_group (session_template_group);

	set_keep_above (true);
	set_resizable (false);
	set_position (WIN_POS_CENTER);
	set_border_width (12);

	sys::path icon_file;

	if (!find_file_in_search_path (ardour_search_path() + system_data_search_path().add_subdirectory_to_paths("icons"), "ardour_icon_48px.png", icon_file)) {
		throw failed_constructor();
	}

	try {
		icon_pixbuf = Gdk::Pixbuf::create_from_file (icon_file.to_string());
	}

	catch (...) {
		throw failed_constructor();
	}

	sys::path been_here_before = user_config_directory();
	been_here_before /= ".a3"; // XXXX use more specific version so we can catch upgrades
	new_user = !exists (been_here_before);

	bool need_audio_setup = !EngineControl::engine_running();

	if (new_user) {
		/* "touch" the file */
		ofstream fout (been_here_before.to_string().c_str());
		setup_new_user_page ();
		setup_first_time_config_page ();
		setup_monitoring_choice_page ();
		setup_monitor_section_choice_page ();

		if (need_audio_setup) {
			setup_audio_page ();
		}

	} else {

		if (need_audio_setup) {
			setup_audio_page ();
		}

		setup_initial_choice_page ();
	}

	setup_session_page ();
	setup_more_options_page ();

	if (new_user) {
		setup_final_page ();
	}

	the_startup = this;
}

ArdourStartup::~ArdourStartup ()
{
}

void
ArdourStartup::set_new_only (bool yn)
{
	new_only = yn;

	if (new_only) {
		ic_vbox.hide ();
	} else {
		ic_vbox.show ();
	}
}

bool
ArdourStartup::use_session_template ()
{
	if (use_template_button.get_active()) {
		return template_chooser.get_active_row_number() > 0;
	} else {
		return !session_template_chooser.get_filename().empty();
	}
}

Glib::ustring
ArdourStartup::session_template_name ()
{
	if (ic_existing_session_button.get_active()) {
		return ustring();
	}

	if (use_template_button.get_active()) {
		TreeModel::iterator iter = template_chooser.get_active ();
		TreeModel::Row row = (*iter);
		string s = row[session_template_columns.path];
		return s;
	} else {
		return session_template_chooser.get_filename();

	}
}

Glib::ustring
ArdourStartup::session_name (bool& should_be_new)
{
	if (ic_new_session_button.get_active()) {
		should_be_new = true;
		return new_name_entry.get_text ();
	} else {
		should_be_new = false;

		TreeIter iter = recent_session_display.get_selection()->get_selected();

		if (iter) {
			return (*iter)[recent_session_columns.visible_name];
		}

		return "";
	}
}

Glib::ustring
ArdourStartup::session_folder ()
{
	if (ic_new_session_button.get_active()) {
		Glib::ustring legal_session_folder_name = legalize_for_path (new_name_entry.get_text());
		return Glib::build_filename (new_folder_chooser.get_current_folder(), legal_session_folder_name);
	} else {
		TreeIter iter = recent_session_display.get_selection()->get_selected();

		if (iter) {
			return (*iter)[recent_session_columns.fullpath];
		}
		return "";
	}
}

void
ArdourStartup::setup_audio_page ()
{
	engine_dialog = manage (new EngineControl);

	engine_dialog->set_border_width (12);

	engine_dialog->show_all ();

	audio_page_index = append_page (*engine_dialog);
	set_page_type (*engine_dialog, ASSISTANT_PAGE_CONTENT);
	set_page_title (*engine_dialog, _("Audio Setup"));

	/* the default parameters should work, so the page is potentially complete */

	set_page_complete (*engine_dialog, true);
}

void
ArdourStartup::setup_new_user_page ()
{
	Label* foomatic = manage (new Label);

	foomatic->set_markup (string_compose (_("\
<span size=\"larger\">%1 is a digital audio workstation. You can use it to\n\
record, edit and mix multi-track audio. You can produce your\n\
own CDs, mix video soundtracks, or just experiment with new\n\
ideas about music and sound.\n\
\n\
There are a few things that need to configured before you start\n\
using the program.</span>\
"), PROGRAM_NAME));

	HBox* hbox = manage (new HBox);
	HBox* vbox = manage (new HBox);

	vbox->set_border_width (24);

	hbox->pack_start (*foomatic, true, true);
	vbox->pack_start (*hbox, true, true);

	foomatic->show ();
	hbox->show ();
	vbox->show ();

	new_user_page_index = append_page (*vbox);
	set_page_type (*vbox, ASSISTANT_PAGE_INTRO);
	set_page_title (*vbox, _("Welcome to Ardour"));
	set_page_header_image (*vbox, icon_pixbuf);
	set_page_complete (*vbox, true);
}

void
ArdourStartup::default_dir_changed ()
{
	Config->set_default_session_parent_dir (default_dir_chooser->get_current_folder());
        config_changed ();
}

void
ArdourStartup::config_changed ()
{
	config_modified = true;
}

void
ArdourStartup::setup_first_time_config_page ()
{
	default_dir_chooser = manage (new FileChooserButton (string_compose (_("Default folder for %1 sessions"), PROGRAM_NAME),
							     FILE_CHOOSER_ACTION_SELECT_FOLDER));
	Gtk::Label* txt = manage (new Label);
	HBox* hbox = manage (new HBox);
	VBox* vbox = manage (new VBox);

	txt->set_markup (_("\
Each project that you work on with Ardour has its own folder.\n\
These can require a lot of disk space if you are recording audio.\n\
\n\
Where would you like new Ardour sessions to be stored by default?\n\n\
<i>(You can put new sessions anywhere, this is just a default)</i>"));
	txt->set_alignment (0.0, 0.0);

	vbox->set_spacing (18);
	vbox->set_border_width (24);

	hbox->pack_start (*default_dir_chooser, false, true, 8);
	vbox->pack_start (*txt, false, false);
	vbox->pack_start (*hbox, false, true);

	default_dir_chooser->set_current_folder (poor_mans_glob (Config->get_default_session_parent_dir()));
	default_dir_chooser->signal_current_folder_changed().connect (sigc::mem_fun (*this, &ArdourStartup::default_dir_changed));
	default_dir_chooser->show ();

	vbox->show_all ();

	default_folder_page_index = append_page (*vbox);
	set_page_title (*vbox, _("Default folder for new sessions"));
	set_page_header_image (*vbox, icon_pixbuf);
	set_page_type (*vbox, ASSISTANT_PAGE_CONTENT);

	/* user can just skip all these settings if they want to */

	set_page_complete (*vbox, true);
}

void
ArdourStartup::setup_monitoring_choice_page ()
{
	mon_vbox.set_spacing (18);
	mon_vbox.set_border_width (24);

	HBox* hbox = manage (new HBox);
	VBox* vbox = manage (new VBox);
	RadioButton::Group g (monitor_via_hardware_button.get_group());
	monitor_via_ardour_button.set_group (g);

	monitor_label.set_markup("\
While recording instruments or vocals, you probably want to listen to the\n\
signal as well as record it. This is called \"monitoring\". There are\n\
different ways to do this depending on the equipment you have and the\n\
configuration of that equipment. The two most common are presented here.\n\
Please choose whichever one is right for your setup.\n\n\
<i>(You can change this preference at any time, via the Preferences dialog)</i>");
	monitor_label.set_alignment (0.0, 0.0);

	vbox->set_spacing (6);

	vbox->pack_start (monitor_via_hardware_button, false, true);
	vbox->pack_start (monitor_via_ardour_button, false, true);
	hbox->pack_start (*vbox, true, true, 8);
	mon_vbox.pack_start (monitor_label, false, false);
	mon_vbox.pack_start (*hbox, false, false);

	mon_vbox.show_all ();

	monitoring_page_index = append_page (mon_vbox);
	set_page_title (mon_vbox, _("Monitoring Choices"));
	set_page_header_image (mon_vbox, icon_pixbuf);

	/* user could just click on "Forward" if default
	 * choice is correct.
	 */

	set_page_complete (mon_vbox, true);
}

void
ArdourStartup::setup_monitor_section_choice_page ()
{
	mon_sec_vbox.set_spacing (18);
	mon_sec_vbox.set_border_width (24);

	HBox* hbox = manage (new HBox);
	VBox* vbox = manage (new VBox);
	RadioButton::Group g (use_monitor_section_button.get_group());
	no_monitor_section_button.set_group (g);

        if (Config->get_use_monitor_bus()) {
                use_monitor_section_button.set_active (true);
        } else {
                no_monitor_section_button.set_active (true);
        }

        use_monitor_section_button.signal_toggled().connect (sigc::mem_fun (*this, &ArdourStartup::config_changed));
        no_monitor_section_button.signal_toggled().connect (sigc::mem_fun (*this, &ArdourStartup::config_changed));
        
	monitor_section_label.set_markup("\
When connecting speakers to Ardour, would you prefer to use a monitor bus,\n\
which will offer various kinds of control at the last stage of output\n\
or would you prefer to just connect directly to the master outs?\n\n\
Most home studio users will probably want to start <i>without</i> a monitor bus.\n\
Those with experience of traditional mixing consoles may prefer to use one.\n\
Please choose whichever one is right for your setup.\n\n\
<i>(You can change this preference at any time, via the Preferences dialog)</i>");
	monitor_section_label.set_alignment (0.0, 0.0);

	vbox->set_spacing (6);

	vbox->pack_start (no_monitor_section_button, false, true);
	vbox->pack_start (use_monitor_section_button, false, true);
	hbox->pack_start (*vbox, true, true, 8);
	mon_sec_vbox.pack_start (monitor_section_label, false, false);
	mon_sec_vbox.pack_start (*hbox, false, false);

	mon_sec_vbox.show_all ();

	monitor_section_page_index = append_page (mon_sec_vbox);
	set_page_title (mon_sec_vbox, _("Monitor Section"));
	set_page_header_image (mon_sec_vbox, icon_pixbuf);

	/* user could just click on "Forward" if default
	 * choice is correct.
	 */

	set_page_complete (mon_sec_vbox, true);
}

void
ArdourStartup::setup_initial_choice_page ()
{
	ic_vbox.set_spacing (6);
	ic_vbox.set_border_width (24);

	RadioButton::Group g (ic_new_session_button.get_group());
	ic_existing_session_button.set_group (g);

	HBox* centering_hbox = manage (new HBox);
	VBox* centering_vbox = manage (new VBox);

	centering_vbox->set_spacing (6);

	centering_vbox->pack_start (ic_new_session_button, false, true);
	centering_vbox->pack_start (ic_existing_session_button, false, true);

	ic_new_session_button.signal_button_press_event().connect(sigc::mem_fun(*this, &ArdourStartup::initial_button_press), false);
	ic_new_session_button.signal_activate().connect(sigc::mem_fun(*this, &ArdourStartup::initial_button_activated), false);

	ic_existing_session_button.signal_button_press_event().connect(sigc::mem_fun(*this, &ArdourStartup::initial_button_press), false);
	ic_existing_session_button.signal_activate().connect(sigc::mem_fun(*this, &ArdourStartup::initial_button_activated), false);

	centering_hbox->pack_start (*centering_vbox, true, true);

	ic_vbox.pack_start (*centering_hbox, true, true);

	ic_vbox.show_all ();

	initial_choice_index = append_page (ic_vbox);
	set_page_title (ic_vbox, _("What would you like to do ?"));
	set_page_header_image (ic_vbox, icon_pixbuf);

	/* user could just click on "Forward" if default
	 * choice is correct.
	 */

	set_page_complete (ic_vbox, true);
}

bool
ArdourStartup::initial_button_press (GdkEventButton *event)
{
	if (event && event->type == GDK_2BUTTON_PRESS && session_page_index != -1)
	{
		set_current_page(session_page_index);
		return true;
	} else {
		return false;
	}
}

void
ArdourStartup::initial_button_activated ()
{
	set_current_page(session_page_index);
}

void
ArdourStartup::setup_session_page ()
{
	session_vbox.set_border_width (24);

	session_vbox.pack_start (session_hbox, true, true);
	session_vbox.show_all ();

	session_page_index = append_page (session_vbox);
	/* initial setting */
	set_page_type (session_vbox, ASSISTANT_PAGE_CONFIRM);
}

void
ArdourStartup::setup_final_page ()
{
	final_page.set_text (string_compose (_("%1 is ready for use"), PROGRAM_NAME));
	final_page.show ();
	final_page_index = append_page (final_page);
	set_page_complete (final_page, true);
	set_page_header_image (final_page, icon_pixbuf);
	set_page_type (final_page, ASSISTANT_PAGE_CONFIRM);
}

void
ArdourStartup::on_cancel ()
{
	_response = RESPONSE_CANCEL;
	gtk_main_quit ();
}

bool
ArdourStartup::on_delete_event (GdkEventAny*)
{
	_response = RESPONSE_CLOSE;
	gtk_main_quit ();
	return true;
}

void
ArdourStartup::on_apply ()
{
	if (engine_dialog) {
		engine_dialog->setup_engine ();
	}

	if (config_modified) {

		if (default_dir_chooser) {
			Config->set_default_session_parent_dir (default_dir_chooser->get_current_folder());
		}

		if (monitor_via_hardware_button.get_active()) {
			Config->set_monitoring_model (ExternalMonitoring);
		} else if (monitor_via_ardour_button.get_active()) {
			Config->set_monitoring_model (SoftwareMonitoring);
		}

                Config->set_use_monitor_bus (use_monitor_section_button.get_active());

		Config->save_state ();
	}

	_response = RESPONSE_OK;
	gtk_main_quit ();
}

void
ArdourStartup::on_prepare (Gtk::Widget* page)
{
	if (page == &session_vbox) {

		if (ic_new_session_button.get_active()) {
			/* new session requested */
			setup_new_session_page ();
		} else {
			/* existing session requested */
			setup_existing_session_page ();
		}
	}
}

void
ArdourStartup::populate_session_templates ()
{
	vector<TemplateInfo> templates;

	find_session_templates (templates);

	template_model->clear ();

	for (vector<TemplateInfo>::iterator x = templates.begin(); x != templates.end(); ++x) {
		TreeModel::Row row;

		row = *(template_model->append ());

		row[session_template_columns.name] = (*x).name;
		row[session_template_columns.path] = (*x).path;
	}
}

static bool
lost_name_entry_focus (GdkEventFocus* ev)
{
        cerr << "lost focus\n";
        return false;
}
                
void
ArdourStartup::setup_new_session_page ()
{
	if (!session_hbox.get_children().empty()) {
		session_hbox.remove (**session_hbox.get_children().begin());
	}

	session_new_vbox.set_spacing (18);

	if (session_new_vbox.get_children().empty()) {
		VBox *vbox1 = manage (new VBox);
		HBox* hbox1 = manage (new HBox);
		Label* label1 = manage (new Label);

		vbox1->set_spacing (6);

		hbox1->set_spacing (6);
		hbox1->pack_start (*label1, false, false);
		hbox1->pack_start (new_name_entry, true, true);

		label1->set_text (_("Session name:"));


		if (!ARDOUR_COMMAND_LINE::session_name.empty()) {
			new_name_entry.set_text  (Glib::path_get_basename (ARDOUR_COMMAND_LINE::session_name));
			/* name provided - they can move right along */
			set_page_complete (session_vbox, true);
		}

		new_name_entry.signal_changed().connect (sigc::mem_fun (*this, &ArdourStartup::new_name_changed));
		new_name_entry.signal_activate().connect (sigc::mem_fun (*this, &ArdourStartup::move_along_now));

		vbox1->pack_start (*hbox1, true, true);

		/* --- */

		HBox* hbox2 = manage (new HBox);
		Label* label2 = manage (new Label);

		hbox2->set_spacing (6);
		hbox2->pack_start (*label2, false, false);
		hbox2->pack_start (new_folder_chooser, true, true);

		label2->set_text (_("Create session folder in:"));

		if (!ARDOUR_COMMAND_LINE::session_name.empty()) {
			new_folder_chooser.set_current_folder (poor_mans_glob (Glib::path_get_dirname (ARDOUR_COMMAND_LINE::session_name)));
		} else {
			new_folder_chooser.set_current_folder (poor_mans_glob (Config->get_default_session_parent_dir()));
		}
		new_folder_chooser.set_title (_("Select folder for session"));

		vbox1->pack_start (*hbox2, false, false);

		session_new_vbox.pack_start (*vbox1, false, false);

		/* --- */

		VBox *vbox2 = manage (new VBox);
		HBox* hbox3 = manage (new HBox);
		Label* label3 = manage (new Label);
		template_model = ListStore::create (session_template_columns);
		populate_session_templates ();

		vbox2->set_spacing (6);

		label3->set_markup (_("<b>Options</b>"));
		label3->set_alignment (0.0, 0.0);

		vbox2->pack_start (*label3, false, true);

		VBox *vbox3 = manage (new VBox);

		vbox3->set_spacing (6);

		if (!template_model->children().empty()) {

			HBox* hbox4a = manage (new HBox);
			use_template_button.set_label (_("Use this template"));

			TreeModel::Row row = *template_model->prepend ();
			row[session_template_columns.name] = (_("no template"));
			row[session_template_columns.path] = string();

			hbox4a->set_spacing (6);
			hbox4a->pack_start (use_template_button, false, false);
			hbox4a->pack_start (template_chooser, true, true);

			template_chooser.set_model (template_model);

			Gtk::CellRendererText* text_renderer = Gtk::manage (new Gtk::CellRendererText);
			text_renderer->property_editable() = false;

			template_chooser.pack_start (*text_renderer);
			template_chooser.add_attribute (text_renderer->property_text(), session_template_columns.name);
			template_chooser.set_active (0);

			use_template_button.show();
			template_chooser.show ();

			vbox3->pack_start (*hbox4a, false, false);
		}

		/* --- */

		if (!new_user) {
			session_template_chooser.set_current_folder (poor_mans_glob (Config->get_default_session_parent_dir()));

			HBox* hbox4b = manage (new HBox);
			use_session_as_template_button.set_label (_("Use an existing session as a template:"));

			hbox4b->set_spacing (6);
			hbox4b->pack_start (use_session_as_template_button, false, false);
			hbox4b->pack_start (session_template_chooser, true, true);

			use_session_as_template_button.show ();
			session_template_chooser.show ();

			Gtk::FileFilter* template_filter = manage (new (Gtk::FileFilter));
			template_filter->add_pattern(X_("*.template"));
			session_template_chooser.set_filter (*template_filter);
			session_template_chooser.set_title (_("Select template"));

			vbox3->pack_start (*hbox4b, false, false);
		}

		/* --- */

		HBox* hbox5 = manage (new HBox);

		hbox5->set_spacing (6);
		hbox5->pack_start (more_new_session_options_button, false, false);

		more_new_session_options_button.show ();
		more_new_session_options_button.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::more_new_session_options_button_clicked));

		vbox3->pack_start (*hbox5, false, false);
		hbox3->pack_start (*vbox3, true, true, 8);
		vbox2->pack_start (*hbox3, false, false);

		/* --- */

		session_new_vbox.pack_start (*vbox2, false, false);
	}

	session_new_vbox.show_all ();
	session_hbox.pack_start (session_new_vbox, true, true);
	set_page_title (session_vbox, _("New Session"));
	set_page_type (session_vbox, ASSISTANT_PAGE_CONFIRM);

	new_name_entry.signal_map().connect (sigc::mem_fun (*this, &ArdourStartup::new_name_mapped));
        new_name_entry.signal_focus_out_event().connect (sigc::ptr_fun (lost_name_entry_focus));
}

void
ArdourStartup::new_name_mapped ()
{
        cerr << "Grab new name focus\n";
	new_name_entry.grab_focus ();
}

void
ArdourStartup::new_name_changed ()
{
	if (!new_name_entry.get_text().empty()) {
		set_page_complete (session_vbox, true);
	} else {
		set_page_complete (session_vbox, false);
	}
}

int
ArdourStartup::redisplay_recent_sessions ()
{
	std::vector<sys::path> session_directories;
	RecentSessionsSorter cmp;

	recent_session_display.set_model (Glib::RefPtr<TreeModel>(0));
	recent_session_model->clear ();

	ARDOUR::RecentSessions rs;
	ARDOUR::read_recent_sessions (rs);

	if (rs.empty()) {
		recent_session_display.set_model (recent_session_model);
		return 0;
	}
	//
	// sort them alphabetically
	sort (rs.begin(), rs.end(), cmp);

	for (ARDOUR::RecentSessions::iterator i = rs.begin(); i != rs.end(); ++i) {
	        session_directories.push_back ((*i).second);
	}

	for (vector<sys::path>::const_iterator i = session_directories.begin(); i != session_directories.end(); ++i)
	{
		std::vector<sys::path> state_file_paths;

		// now get available states for this session

		get_state_files_in_directory (*i, state_file_paths);

		vector<string*>* states;
		vector<const gchar*> item;
		string fullpath = (*i).to_string();

		/* remove any trailing / */

		if (fullpath[fullpath.length()-1] == '/') {
			fullpath = fullpath.substr (0, fullpath.length()-1);
		}

		/* check whether session still exists */
		if (!Glib::file_test(fullpath.c_str(), Glib::FILE_TEST_EXISTS)) {
			/* session doesn't exist */
			continue;
		}

		/* now get available states for this session */

		if ((states = Session::possible_states (fullpath)) == 0) {
			/* no state file? */
			continue;
		}

		std::vector<string> state_file_names(get_file_names_no_extension (state_file_paths));

		Gtk::TreeModel::Row row = *(recent_session_model->append());

		row[recent_session_columns.visible_name] = Glib::path_get_basename (fullpath);
		row[recent_session_columns.fullpath] = fullpath;

		if (state_file_names.size() > 1) {

			// add the children

			for (std::vector<std::string>::iterator i2 = state_file_names.begin();
					i2 != state_file_names.end(); ++i2)
			{

				Gtk::TreeModel::Row child_row = *(recent_session_model->append (row.children()));

				child_row[recent_session_columns.visible_name] = *i2;
				child_row[recent_session_columns.fullpath] = fullpath;
			}
		}
	}

	recent_session_display.set_model (recent_session_model);
	return rs.size();
}

void
ArdourStartup::recent_session_row_selected ()
{
	if (recent_session_display.get_selection()->count_selected_rows() > 0) {
		set_page_complete (session_vbox, true);
	} else {
		set_page_complete (session_vbox, false);
	}
}

void
ArdourStartup::setup_existing_session_page ()
{
	if (!session_hbox.get_children().empty()) {
		session_hbox.remove (**session_hbox.get_children().begin());
	}

	if (recent_scroller.get_children().empty()) {

		recent_session_model = TreeStore::create (recent_session_columns);
		recent_session_display.set_model (recent_session_model);
		recent_session_display.append_column (_("Recent Sessions"), recent_session_columns.visible_name);
		recent_session_display.set_headers_visible (false);
		recent_session_display.get_selection()->set_mode (SELECTION_BROWSE);

		recent_session_display.get_selection()->signal_changed().connect (sigc::mem_fun (*this, &ArdourStartup::recent_session_row_selected));

		recent_scroller.add (recent_session_display);
		recent_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
		recent_scroller.set_shadow_type	(Gtk::SHADOW_IN);

		recent_session_display.show();
	}

	recent_scroller.show();
	int cnt = redisplay_recent_sessions ();
	recent_session_display.signal_row_activated().connect (sigc::mem_fun (*this, &ArdourStartup::recent_row_activated));

	if (cnt > 4) {
		recent_scroller.set_size_request (-1, 300);
	}

	session_hbox.pack_start (recent_scroller, true, true);
	set_page_title (session_vbox, _("Select a session"));
	set_page_type (session_vbox, ASSISTANT_PAGE_CONFIRM);
}

void
ArdourStartup::more_new_session_options_button_clicked ()
{
	if (more_new_session_options_button.get_active()) {
		more_options_vbox.show_all ();
		set_page_type (more_options_vbox, ASSISTANT_PAGE_CONFIRM);
		set_page_type (session_vbox, ASSISTANT_PAGE_CONTENT);
	} else {
		set_page_type (session_vbox, ASSISTANT_PAGE_CONFIRM);
		more_options_vbox.hide ();
	}
}

void
ArdourStartup::setup_more_options_page ()
{
	more_options_vbox.set_border_width (24);

	_output_limit_count.set_adjustment (_output_limit_count_adj);
	_input_limit_count.set_adjustment (_input_limit_count_adj);
	_master_bus_channel_count.set_adjustment (_master_bus_channel_count_adj);

	chan_count_label_1.set_text (_("channels"));
	chan_count_label_3.set_text (_("channels"));
	chan_count_label_4.set_text (_("channels"));

	chan_count_label_1.set_alignment(0,0.5);
	chan_count_label_1.set_padding(0,0);
	chan_count_label_1.set_line_wrap(false);

	chan_count_label_3.set_alignment(0,0.5);
	chan_count_label_3.set_padding(0,0);
	chan_count_label_3.set_line_wrap(false);

	chan_count_label_4.set_alignment(0,0.5);
	chan_count_label_4.set_padding(0,0);
	chan_count_label_4.set_line_wrap(false);

	bus_label.set_markup (_("<b>Busses</b>"));
	input_label.set_markup (_("<b>Inputs</b>"));
	output_label.set_markup (_("<b>Outputs</b>"));

	_master_bus_channel_count.set_flags(Gtk::CAN_FOCUS);
	_master_bus_channel_count.set_update_policy(Gtk::UPDATE_ALWAYS);
	_master_bus_channel_count.set_numeric(true);
	_master_bus_channel_count.set_digits(0);
	_master_bus_channel_count.set_wrap(false);

	_create_master_bus.set_label (_("Create master bus"));
	_create_master_bus.set_flags(Gtk::CAN_FOCUS);
	_create_master_bus.set_relief(Gtk::RELIEF_NORMAL);
	_create_master_bus.set_mode(true);
	_create_master_bus.set_active(true);
	_create_master_bus.set_border_width(0);

	advanced_table.set_row_spacings(0);
	advanced_table.set_col_spacings(0);

	_connect_inputs.set_label (_("Automatically connect to physical_inputs"));
	_connect_inputs.set_flags(Gtk::CAN_FOCUS);
	_connect_inputs.set_relief(Gtk::RELIEF_NORMAL);
	_connect_inputs.set_mode(true);
	_connect_inputs.set_active(true);
	_connect_inputs.set_border_width(0);

	_limit_input_ports.set_label (_("Use only"));
	_limit_input_ports.set_flags(Gtk::CAN_FOCUS);
	_limit_input_ports.set_relief(Gtk::RELIEF_NORMAL);
	_limit_input_ports.set_mode(true);
	_limit_input_ports.set_sensitive(true);
	_limit_input_ports.set_border_width(0);

	_input_limit_count.set_flags(Gtk::CAN_FOCUS);
	_input_limit_count.set_update_policy(Gtk::UPDATE_ALWAYS);
	_input_limit_count.set_numeric(true);
	_input_limit_count.set_digits(0);
	_input_limit_count.set_wrap(false);
	_input_limit_count.set_sensitive(false);

	bus_hbox.pack_start (bus_table, Gtk::PACK_SHRINK, 18);

	bus_label.set_alignment(0, 0.5);
	bus_label.set_padding(0,0);
	bus_label.set_line_wrap(false);
	bus_label.set_selectable(false);
	bus_label.set_use_markup(true);
	bus_frame.set_shadow_type(Gtk::SHADOW_NONE);
	bus_frame.set_label_align(0,0.5);
	bus_frame.add(bus_hbox);
	bus_frame.set_label_widget(bus_label);

	bus_table.set_row_spacings (0);
	bus_table.set_col_spacings (0);
	bus_table.attach (_create_master_bus, 0, 1, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0, 0);
	bus_table.attach (_master_bus_channel_count, 1, 2, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0, 0);
	bus_table.attach (chan_count_label_1, 2, 3, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 6, 0);

	input_port_limit_hbox.pack_start(_limit_input_ports, Gtk::PACK_SHRINK, 6);
	input_port_limit_hbox.pack_start(_input_limit_count, Gtk::PACK_SHRINK, 0);
	input_port_limit_hbox.pack_start(chan_count_label_3, Gtk::PACK_SHRINK, 6);
	input_port_vbox.pack_start(_connect_inputs, Gtk::PACK_SHRINK, 0);
	input_port_vbox.pack_start(input_port_limit_hbox, Gtk::PACK_EXPAND_PADDING, 0);
	input_table.set_row_spacings(0);
	input_table.set_col_spacings(0);
	input_table.attach(input_port_vbox, 0, 1, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 6, 6);

	input_hbox.pack_start (input_table, Gtk::PACK_SHRINK, 18);

	input_label.set_alignment(0, 0.5);
	input_label.set_padding(0,0);
	input_label.set_line_wrap(false);
	input_label.set_selectable(false);
	input_label.set_use_markup(true);
	input_frame.set_shadow_type(Gtk::SHADOW_NONE);
	input_frame.set_label_align(0,0.5);
	input_frame.add(input_hbox);
	input_frame.set_label_widget(input_label);

	_connect_outputs.set_label (_("Automatically connect outputs"));
	_connect_outputs.set_flags(Gtk::CAN_FOCUS);
	_connect_outputs.set_relief(Gtk::RELIEF_NORMAL);
	_connect_outputs.set_mode(true);
	_connect_outputs.set_active(true);
	_connect_outputs.set_border_width(0);
	_limit_output_ports.set_label (_("Use only"));
	_limit_output_ports.set_flags(Gtk::CAN_FOCUS);
	_limit_output_ports.set_relief(Gtk::RELIEF_NORMAL);
	_limit_output_ports.set_mode(true);
	_limit_output_ports.set_sensitive(true);
	_limit_output_ports.set_border_width(0);
	_output_limit_count.set_flags(Gtk::CAN_FOCUS);
	_output_limit_count.set_update_policy(Gtk::UPDATE_ALWAYS);
	_output_limit_count.set_numeric(false);
	_output_limit_count.set_digits(0);
	_output_limit_count.set_wrap(false);
	_output_limit_count.set_sensitive(false);
	output_port_limit_hbox.pack_start(_limit_output_ports, Gtk::PACK_SHRINK, 6);
	output_port_limit_hbox.pack_start(_output_limit_count, Gtk::PACK_SHRINK, 0);
	output_port_limit_hbox.pack_start(chan_count_label_4, Gtk::PACK_SHRINK, 6);

	_connect_outputs_to_master.set_label (_("... to master bus"));
	_connect_outputs_to_master.set_flags(Gtk::CAN_FOCUS);
	_connect_outputs_to_master.set_relief(Gtk::RELIEF_NORMAL);
	_connect_outputs_to_master.set_mode(true);
	_connect_outputs_to_master.set_active(false);
	_connect_outputs_to_master.set_border_width(0);

	_connect_outputs_to_master.set_group (connect_outputs_group);
	_connect_outputs_to_physical.set_group (connect_outputs_group);

	_connect_outputs_to_physical.set_label (_("... to physical outputs"));
	_connect_outputs_to_physical.set_flags(Gtk::CAN_FOCUS);
	_connect_outputs_to_physical.set_relief(Gtk::RELIEF_NORMAL);
	_connect_outputs_to_physical.set_mode(true);
	_connect_outputs_to_physical.set_active(false);
	_connect_outputs_to_physical.set_border_width(0);

	output_conn_vbox.pack_start(_connect_outputs, Gtk::PACK_SHRINK, 0);
	output_conn_vbox.pack_start(_connect_outputs_to_master, Gtk::PACK_SHRINK, 0);
	output_conn_vbox.pack_start(_connect_outputs_to_physical, Gtk::PACK_SHRINK, 0);
	output_vbox.set_border_width(6);

	output_port_vbox.pack_start(output_port_limit_hbox, Gtk::PACK_SHRINK, 0);

	output_vbox.pack_start(output_conn_vbox);
	output_vbox.pack_start(output_port_vbox);

	output_label.set_alignment(0, 0.5);
	output_label.set_padding(0,0);
	output_label.set_line_wrap(false);
	output_label.set_selectable(false);
	output_label.set_use_markup(true);
	output_frame.set_shadow_type(Gtk::SHADOW_NONE);
	output_frame.set_label_align(0,0.5);

	output_hbox.pack_start (output_vbox, Gtk::PACK_SHRINK, 18);

	output_frame.add(output_hbox);
	output_frame.set_label_widget(output_label);

	more_options_vbox.pack_start(advanced_table, Gtk::PACK_SHRINK, 0);
	more_options_vbox.pack_start(bus_frame, Gtk::PACK_SHRINK, 6);
	more_options_vbox.pack_start(input_frame, Gtk::PACK_SHRINK, 6);
	more_options_vbox.pack_start(output_frame, Gtk::PACK_SHRINK, 0);

	/* signals */

	_connect_inputs.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::connect_inputs_clicked));
	_connect_outputs.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::connect_outputs_clicked));
	_limit_input_ports.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::limit_inputs_clicked));
	_limit_output_ports.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::limit_outputs_clicked));
	_create_master_bus.signal_clicked().connect (sigc::mem_fun (*this, &ArdourStartup::master_bus_button_clicked));

	/* note that more_options_vbox is NOT visible by
	 * default. this is entirely by design - this page
	 * should be skipped unless explicitly requested.
	 */

	session_options_page_index = append_page (more_options_vbox);
	set_page_title (more_options_vbox, _("Advanced Session Options"));
	set_page_complete (more_options_vbox, true);
}

bool
ArdourStartup::create_master_bus() const
{
	return _create_master_bus.get_active();
}

int
ArdourStartup::master_channel_count() const
{
	return _master_bus_channel_count.get_value_as_int();
}

bool
ArdourStartup::connect_inputs() const
{
	return _connect_inputs.get_active();
}

bool
ArdourStartup::limit_inputs_used_for_connection() const
{
	return _limit_input_ports.get_active();
}

int
ArdourStartup::input_limit_count() const
{
	return _input_limit_count.get_value_as_int();
}

bool
ArdourStartup::connect_outputs() const
{
	return _connect_outputs.get_active();
}

bool
ArdourStartup::limit_outputs_used_for_connection() const
{
	return _limit_output_ports.get_active();
}

int
ArdourStartup::output_limit_count() const
{
	return _output_limit_count.get_value_as_int();
}

bool
ArdourStartup::connect_outs_to_master() const
{
	return _connect_outputs_to_master.get_active();
}

bool
ArdourStartup::connect_outs_to_physical() const
{
	return _connect_outputs_to_physical.get_active();
}

void
ArdourStartup::connect_inputs_clicked ()
{
	_limit_input_ports.set_sensitive(_connect_inputs.get_active());

	if (_connect_inputs.get_active() && _limit_input_ports.get_active()) {
		_input_limit_count.set_sensitive(true);
	} else {
		_input_limit_count.set_sensitive(false);
	}
}

void
ArdourStartup::connect_outputs_clicked ()
{
	_limit_output_ports.set_sensitive(_connect_outputs.get_active());

	if (_connect_outputs.get_active() && _limit_output_ports.get_active()) {
		_output_limit_count.set_sensitive(true);
	} else {
		_output_limit_count.set_sensitive(false);
	}
}

void
ArdourStartup::limit_inputs_clicked ()
{
	_input_limit_count.set_sensitive(_limit_input_ports.get_active());
}

void
ArdourStartup::limit_outputs_clicked ()
{
	_output_limit_count.set_sensitive(_limit_output_ports.get_active());
}

void
ArdourStartup::master_bus_button_clicked ()
{
        bool yn = _create_master_bus.get_active();

	_master_bus_channel_count.set_sensitive(yn);
}

void
ArdourStartup::move_along_now ()
{
	gint cur = get_current_page ();

	if (cur == session_page_index) {
		if (more_new_session_options_button.get_active()) {
			set_current_page (session_options_page_index);
		} else {
			on_apply ();
		}
	}
}

void
ArdourStartup::recent_row_activated (const Gtk::TreePath&, Gtk::TreeViewColumn*)
{
	set_page_complete (session_vbox, true);
	move_along_now ();
}
