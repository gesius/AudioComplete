/* sfdb_freesound_mootcher.cpp **********************************************************************

	Adapted for Ardour by Ben Loftis, March 2008
	Updated to new Freesound API by Colin Fletcher, November 2011

	Mootcher 23-8-2005

	Mootcher Online Access to thefreesoundproject website
	http://freesound.iua.upf.edu/

	GPL 2005 Jorn Lemon
	mail for questions/remarks: mootcher@twistedlemon.nl
	or go to the freesound website forum

	-----------------------------------------------------------------

	Includes:
		curl.h    (version 7.14.0)
	Librarys:
		libcurl.lib

	-----------------------------------------------------------------
	Licence GPL:

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


*************************************************************************************/
#include "sfdb_freesound_mootcher.h"

#include "pbd/xml++.h"
#include "pbd/filesystem.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>

#include "ardour/audio_library.h"

static const std::string base_url = "http://www.freesound.org/api";
static const std::string api_key = "9d77cb8d841b4bcfa960e1aae62224eb"; // ardour3


//------------------------------------------------------------------------
Mootcher::Mootcher(const char *saveLocation)
	: curl(curl_easy_init())
{
	changeWorkingDir(saveLocation);
};
//------------------------------------------------------------------------
Mootcher:: ~Mootcher()
{
}

//------------------------------------------------------------------------
void Mootcher::changeWorkingDir(const char *saveLocation)
{
	basePath = saveLocation;
#ifdef __WIN32__
	std::string replace = "/";
	size_t pos = basePath.find("\\");
	while( pos != std::string::npos ){
		basePath.replace(pos, 1, replace);
		pos = basePath.find("\\");
	}
#endif
	//
	size_t pos2 = basePath.find_last_of("/");
	if(basePath.length() != (pos2+1)) basePath += "/";
}

void Mootcher::ensureWorkingDir ()
{
	PBD::sys::path p = basePath;
	p /= "snd";
	if (!PBD::sys::is_directory (p)) {
		PBD::sys::create_directories (p);
	}
}
	

//------------------------------------------------------------------------
size_t Mootcher::WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = (int)(size * nmemb);
	struct MemoryStruct *mem = (struct MemoryStruct *)data;

	mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);

	if (mem->memory) {
		memcpy(&(mem->memory[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->memory[mem->size] = 0;
	}
	return realsize;
}


//------------------------------------------------------------------------

std::string Mootcher::sortMethodString(enum sortMethod sort) {

	switch (sort) {
		case sort_duration_desc:	return "duration_desc";	
		case sort_duration_asc: 	return "duration_asc";
		case sort_created_desc:		return "created_desc";
		case sort_created_asc:		return "created_asc";
		case sort_downloads_desc:	return "downloads_desc";
		case sort_downloads_asc:	return "downloads_asc";
		case sort_rating_desc:		return "rating_desc";
		case sort_rating_asc:		return "rating_asc";
		default:			return "";	
	}
}

//------------------------------------------------------------------------
void Mootcher::setcUrlOptions()
{
	// basic init for curl
	curl_global_init(CURL_GLOBAL_ALL);
	// some servers don't like requests that are made without a user-agent field, so we provide one
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	// setup curl error buffer
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	// Allow redirection
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	
	// Allow connections to time out (without using signals)
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);


}

std::string Mootcher::doRequest(std::string uri, std::string params)
{
	std::string result;
	struct MemoryStruct xml_page;
	xml_page.memory = NULL;
	xml_page.size = 0;

	setcUrlOptions();
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &xml_page);

	// curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
	// curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postMessage.c_str());
	// curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1);

	// the url to get
	std::string url = base_url + uri + "?";
	if (params != "") {
		url += params + "&api_key=" + api_key + "&format=xml";
	} else {
		url += "api_key=" + api_key + "&format=xml";
	}
		
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str() );
	std::cerr << "doRequest: " << url << std::endl;
	
	// perform online request
	CURLcode res = curl_easy_perform(curl);
	if( res != 0 ) {
		std::cerr << "curl error " << res << " (" << curl_easy_strerror(res) << ")" << std::endl;
		return "";
	}

	result = xml_page.memory;
	// free the memory
	if(xml_page.memory){
		free( xml_page.memory );
		xml_page.memory = NULL;
		xml_page.size = 0;
	}

	return result;

}


std::string Mootcher::searchText(std::string query, int page, std::string filter, enum sortMethod sort)
{
	std::string params = "";
	char buf[24];

	if (page > 1) {
		snprintf(buf, 23, "p=%d&", page);
		params += buf;
	}
	
	params += "q=" + query;	

	if (filter != "")
		params += "&f=" + filter;
	
	if (sort)
		params += "&s=" + sortMethodString(sort);

	return doRequest("/sounds/search", params);
}

//------------------------------------------------------------------------

std::string Mootcher::getSoundResourceFile(std::string ID)
{

	std::string originalSoundURI;
	std::string audioFileName;
	std::string xmlFileName;
	std::string xml;


	std::cerr << "getSoundResourceFile(" << ID << ")" << std::endl;

	// download the xmlfile into xml_page
	xml = doRequest("/sounds/" + ID, "");

	XMLTree doc;
	doc.read_buffer( xml.c_str() );
	XMLNode *freesound = doc.root();

	// if the page is not a valid xml document with a 'freesound' root
	if (freesound == NULL) {
		std::cerr << "getSoundResourceFile: There is no valid root in the xml file" << std::endl;
		return "";
	}

	if (strcmp(doc.root()->name().c_str(), "response") != 0) {
		std::cerr << "getSoundResourceFile: root =" << doc.root()->name() << ", != response" << std::endl;
		return "";
	}

	XMLNode *name = freesound->child("original_filename");
	XMLNode *filesize = freesound->child("filesize");


	// get the file name and size from xml file
	if (name && filesize) {

		audioFileName = basePath + "snd/" + ID + "-" + name->child("text")->content();

		// create new filename with the ID number
		xmlFileName = basePath;
		xmlFileName += "snd/";
		xmlFileName += freesound->child("id")->child("text")->content();
		xmlFileName += "-";
		xmlFileName += name->child("text")->content();
		xmlFileName += ".xml";

		// std::cerr << "getSoundResourceFile: saving XML: " << xmlFileName << std::endl;

		// save the xml file to disk
		ensureWorkingDir();
		doc.write(xmlFileName.c_str());

		//store all the tags in the database
		XMLNode *tags = freesound->child("tags");
		if (tags) {
			XMLNodeList children = tags->children();
			XMLNodeConstIterator niter;
			std::vector<std::string> strings;
			for (niter = children.begin(); niter != children.end(); ++niter) {
				XMLNode *node = *niter;
				if( strcmp( node->name().c_str(), "resource") == 0 ) {
					XMLNode *text = node->child("text");
					if (text) {
						// std::cerr << "tag: " << text->content() << std::endl;
						strings.push_back(text->content());
					}
				}
			}
			ARDOUR::Library->set_tags (std::string("//")+audioFileName, strings);
			ARDOUR::Library->save_changes ();
		}
	}

	return audioFileName;
}

int audioFileWrite(void *buffer, size_t size, size_t nmemb, void *file)
{
	return (int)fwrite(buffer, size, nmemb, (FILE*) file);
};

//------------------------------------------------------------------------
std::string Mootcher::getAudioFile(std::string originalFileName, std::string ID, std::string audioURL, Gtk::ProgressBar *progress_bar)
{
	ensureWorkingDir();
	std::string audioFileName = basePath + "snd/" + ID + "-" + originalFileName;

	//check to see if audio file already exists
	FILE *testFile = fopen(audioFileName.c_str(), "r");
	if (testFile) {  
		fseek (testFile , 0 , SEEK_END);
		if (ftell (testFile) > 256) {
			std::cerr << "audio file " << audioFileName << " already exists" << std::endl;
			fclose (testFile);
			return audioFileName;
		}
		
		// else file was small, probably an error, delete it and try again
		fclose(testFile);
		remove( audioFileName.c_str() );  
	}

	//now download the actual file
	if (curl) {

		FILE* theFile;
		theFile = fopen( audioFileName.c_str(), "wb" );

		if (theFile) {
		
			// create the download url
			audioURL += "?api_key=" + api_key;
		
			setcUrlOptions();
			curl_easy_setopt(curl, CURLOPT_URL, audioURL.c_str() );
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, audioFileWrite);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, theFile);

			std::cerr << "downloading " << audioFileName << " from " << audioURL << "..." << std::endl;

			curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0); // turn on the progress bar
			curl_easy_setopt (curl, CURLOPT_PROGRESSFUNCTION, progress_callback);
			curl_easy_setopt (curl, CURLOPT_PROGRESSDATA, progress_bar);

			CURLcode res = curl_easy_perform(curl);
			fclose(theFile);

			curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 1); // turn off the progress bar
			progress_bar->set_fraction(0.0);

			if( res != 0 ) {
				std::cerr <<  "curl error " << res << " (" << curl_easy_strerror(res) << ")" << std::endl;
				remove( audioFileName.c_str() );  
				return "";
			} else {
				std::cerr << "done!" << std::endl;
				// now download the tags &c.
				getSoundResourceFile(ID);
			}
		}
	}

	return audioFileName;
}

//---------
int Mootcher::progress_callback(void *bar, double dltotal, double dlnow, double ultotal, double ulnow)
{

	//XXX I hope it's OK to do GTK things in this callback. Otherwise
	// I'll have to do stuff like in interthread_progress_window.
	
	Gtk::ProgressBar *progress_bar = (Gtk::ProgressBar *) bar;
	progress_bar->set_fraction(dlnow/dltotal);
	/* Make sure the progress widget gets updated */
	while (Glib::MainContext::get_default()->iteration (false)) {
		/* do nothing */
	}
	std::cerr << "progress: " << dlnow << " of " << dltotal << " \r";
	return 0;
}

