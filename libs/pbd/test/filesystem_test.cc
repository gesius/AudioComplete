#include <unistd.h>
#include <stdlib.h>
#include "filesystem_test.h"
#include "pbd/filesystem.h"

using namespace std;

CPPUNIT_TEST_SUITE_REGISTRATION (FilesystemTest);

void
FilesystemTest::testPathIsWithin ()
{
	system ("rm -r foo");
	PBD::sys::create_directories ("foo/bar/baz");

	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar/baz", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar"));

	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar/baz", "frobozz") == false);

	int const r = symlink ("bar", "foo/jim");
	CPPUNIT_ASSERT (r == 0);

	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar/baz", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar/baz"));
	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/bar", "foo/bar"));

	CPPUNIT_ASSERT (PBD::sys::path_is_within ("foo/jim/baz", "frobozz") == false);
}

