/*sfdb_freesound_mootcher.h****************************************************************************

	Adapted for Ardour by Ben Loftis, March 2008
	Updated to new Freesound API by Colin Fletcher, November 2011

	Mootcher Online Access to thefreesoundproject website
	http://freesound.iua.upf.edu/

	GPL 2005 Jorn Lemon
	mail for questions/remarks: mootcher@twistedlemon.nl
	or go to the freesound website forum

*****************************************************************************/

#include <string>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <gtkmm/progressbar.h>
//#include <ctime>

#include "curl/curl.h"

//--- struct to store XML file
struct MemoryStruct {
	char *memory;
	size_t size;
};

enum sortMethod {
	sort_none,		// no sort
	sort_duration_desc,	// Sort by the duration of the sounds, longest sounds first.
	sort_duration_asc, 	// Same as above, but shortest sounds first.
	sort_created_desc, 	// Sort by the date of when the sound was added. newest sounds first.
	sort_created_asc, 	// Same as above, but oldest sounds first.
	sort_downloads_desc, 	// Sort by the number of downloads, most downloaded sounds first.
	sort_downloads_asc, 	// Same as above, but least downloaded sounds first.
	sort_rating_desc, 	// Sort by the average rating given to the sounds, highest rated first.
	sort_rating_asc 	// Same as above, but lowest rated sounds first.
};


class Mootcher
{
public:
	Mootcher(const char *saveLocation);
	~Mootcher();

	std::string	getAudioFile(std::string originalFileName, std::string ID, std::string audioURL, Gtk::ProgressBar *progress_bar);
	std::string	searchText(std::string query, int page, std::string filter, enum sortMethod sort);

private:

	const char*	changeWorkingDir(const char *saveLocation);

	std::string	doRequest(std::string uri, std::string params);
	void		setcUrlOptions();

	static size_t	WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data);
	static int	progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
	std::string	sortMethodString(enum sortMethod sort);
	std::string	getSoundResourceFile(std::string ID);

	CURL *curl;
	char errorBuffer[CURL_ERROR_SIZE];	// storage for cUrl error message

	std::string basePath;
	std::string xmlLocation;
};

