
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <stdexcept>

#include "pbd/textreceiver.h"
#include "pbd/file_utils.h"
#include "ardour/session.h"
#include "ardour/audioengine.h"
#include "ardour/smf_source.h"
#include "ardour/midi_model.h"

#include "test_util.h"

#include "session_test.h"

CPPUNIT_TEST_SUITE_REGISTRATION (SessionTest);

using namespace std;
using namespace ARDOUR;
using namespace PBD;

void
SessionTest::new_session ()
{
	const string session_name("test_session");
	std::string new_session_dir = Glib::build_filename (new_test_output_dir(), session_name);

	CPPUNIT_ASSERT (!Glib::file_test (new_session_dir, Glib::FILE_TEST_EXISTS));

	create_and_start_dummy_backend ();

	ARDOUR::Session* new_session = load_session (new_session_dir, "test_session");

	CPPUNIT_ASSERT (new_session);

	new_session->save_state ("");

	delete new_session;
	stop_and_destroy_backend ();
}

void
SessionTest::new_session_from_template ()
{
	const string session_name("two_tracks");
	const string session_template_dir_name("2 Track-template");

	std::string new_session_dir = Glib::build_filename (new_test_output_dir(), session_name);

	CPPUNIT_ASSERT (!Glib::file_test (new_session_dir, Glib::FILE_TEST_EXISTS));

	std::string session_template_dir = test_search_path ().front ();
	session_template_dir = Glib::build_filename (session_template_dir, "2 Track-template");

	CPPUNIT_ASSERT (Glib::file_test (session_template_dir, Glib::FILE_TEST_IS_DIR));

	Session* new_session = 0;
	BusProfile* bus_profile = 0;

	create_and_start_dummy_backend ();

	// create a new session based on session template
	new_session = new Session (*AudioEngine::instance (), new_session_dir, session_name,
				bus_profile, session_template_dir);

	CPPUNIT_ASSERT (new_session);

	new_session->save_state ("");

	delete new_session;
	stop_and_destroy_backend ();

	// keep the same audio backend
	create_and_start_dummy_backend ();

	Session* template_session = 0;

	// reopen same session to check that it opens without error
	template_session = new Session (*AudioEngine::instance (), new_session_dir, session_name);

	CPPUNIT_ASSERT (template_session);

	delete template_session;
	stop_and_destroy_backend ();
}
