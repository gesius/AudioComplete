/***********************************************************/
/*vstfx infofile - module to manage info files             */
/*containing cached information about a plugin. e.g. its   */
/*name, creator etc etc                                    */
/***********************************************************/

#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "ardour/vstfx.h"

#define MAX_STRING_LEN 256

static char* read_string(FILE *fp)
{
    char buf[MAX_STRING_LEN];

    fgets( buf, MAX_STRING_LEN, fp );
	
    if(strlen(buf) < MAX_STRING_LEN)
	{
		if(strlen(buf))
	    	buf[strlen(buf)-1] = 0;

		return strdup(buf);
    }
	else
	{
		return NULL;
    }
}

static VSTFXInfo* load_vstfx_info_file(FILE* fp)
{
	VSTFXInfo *info;
	int i;
	
	if ((info = (VSTFXInfo*) malloc(sizeof(VSTFXInfo))) == NULL) {
		return NULL;
	}

	if((info->name = read_string(fp)) == NULL) goto error;
	if((info->creator = read_string(fp)) == NULL) goto error;
	if(1 != fscanf(fp, "%d\n", &info->UniqueID)) goto error;
	if((info->Category = read_string(fp)) == NULL) goto error;
	if(1 != fscanf(fp, "%d\n", &info->numInputs)) goto error;
	if(1 != fscanf(fp, "%d\n", &info->numOutputs)) goto error;
	if(1 != fscanf(fp, "%d\n", &info->numParams)) goto error;
	if(1 != fscanf(fp, "%d\n", &info->wantMidi)) goto error;
	if(1 != fscanf(fp, "%d\n", &info->hasEditor)) goto error;
	if(1 != fscanf(fp, "%d\n", &info->canProcessReplacing)) goto error;
	
	if((info->ParamNames = (char **) malloc(sizeof(char*)*info->numParams)) == NULL) {
		goto error;
	}

	for (i=0; i<info->numParams; i++) {
		if((info->ParamNames[i] = read_string(fp)) == NULL) goto error;
	}

	if ((info->ParamLabels = (char **) malloc(sizeof(char*)*info->numParams)) == NULL) {
		goto error;
	}
	
	for (i=0; i < info->numParams; i++) {
		if((info->ParamLabels[i] = read_string(fp)) == NULL) goto error;
	}
	
	return info;
	
  error:
	free( info );
	return NULL;
}

static int save_vstfx_info_file(VSTFXInfo *info, FILE* fp)
{
    int i;

    if (info == NULL) {
	    vstfx_error("** ERROR ** VSTFXinfofile : info ptr is NULL\n");
	    return -1;
    }

    if (fp == NULL) {
	    vstfx_error("** ERROR ** VSTFXinfofile : file ptr is NULL\n");
	    return -1;
    }
    
    fprintf( fp, "%s\n", info->name );
    fprintf( fp, "%s\n", info->creator );
    fprintf( fp, "%d\n", info->UniqueID );
    fprintf( fp, "%s\n", info->Category );
    fprintf( fp, "%d\n", info->numInputs );
    fprintf( fp, "%d\n", info->numOutputs );
    fprintf( fp, "%d\n", info->numParams );
    fprintf( fp, "%d\n", info->wantMidi );
    fprintf( fp, "%d\n", info->hasEditor );
    fprintf( fp, "%d\n", info->canProcessReplacing );

    for (i=0; i < info->numParams; i++) {
		fprintf(fp, "%s\n", info->ParamNames[i]);
    }
	
    for (i=0; i < info->numParams; i++) {
		fprintf(fp, "%s\n", info->ParamLabels[i]);
    }
	
    return 0;
}

static char* vstfx_infofile_stat (char *dllpath, struct stat* statbuf, int personal)
{
	char* path;
	char* dir_path;
	char* basename;
	char* base;
	size_t blen;

	if (strstr (dllpath, ".so" ) == NULL) {
		return NULL;
	}
	
	if (personal) {
		dir_path = g_build_filename (g_get_home_dir(), ".fst", NULL);
	} else {
		dir_path = g_path_get_dirname (dllpath);
	}
	
	base = g_path_get_basename (dllpath);
	blen = strlen (base) + 2; // null char and '.'
	basename = (char*) g_malloc (blen);
	snprintf (basename, blen, ".%s.fsi", base);
	g_free (base);

	path = g_build_filename (dir_path, basename, NULL);

	g_free (dir_path);
	g_free (basename);


	if (g_file_test (path, GFileTest (G_FILE_TEST_EXISTS|G_FILE_TEST_IS_REGULAR))) {

		/* info file exists in same location as the shared object, so
		   check if its current and up to date
		*/


		struct stat dllstat;
		
		if (stat (dllpath, &dllstat) == 0) {
			if (stat(path, statbuf) == 0) {
				if (dllstat.st_mtime <= statbuf->st_mtime) {
					/* plugin is older than info file */
					return path;
				}
			}
		} 
	}

	g_free (path);

	return NULL;
}


static FILE* vstfx_infofile_for_read (char* dllpath)
{
	struct stat own_statbuf;
	struct stat sys_statbuf;
	char *own_info;
	char *sys_info;
	
	own_info = vstfx_infofile_stat (dllpath, &own_statbuf, 1);
	sys_info = vstfx_infofile_stat (dllpath, &sys_statbuf, 0);

	if (own_info) {
		if (sys_info) {
			if (own_statbuf.st_mtime <= sys_statbuf.st_mtime) {
				/* system info file is newer, use it */
				return fopen (sys_info, "r");
			}
		} else {
			return fopen (own_info, "r");
		}
	}

	return NULL;
}

static FILE* vstfx_infofile_create (char* dllpath, int personal)
{
	char* path;
	char* dir_path;
	char* basename;
	char* base;
	size_t blen;

	if (strstr (dllpath, ".so" ) == NULL) {
		return NULL;
	}
	
	if (personal) {
		dir_path = g_build_filename (g_get_home_dir(), ".fst", NULL);

		/* if the directory doesn't yet exist, try to create it */

		if (!g_file_test (dir_path, G_FILE_TEST_IS_DIR)) {
			if (g_mkdir (dir_path, 0700)) {
				return NULL;
			}
		}

	} else {
		dir_path = g_path_get_dirname (dllpath);
	}
	
	base = g_path_get_basename (dllpath);
	blen = strlen (base) + 2; // null char and '.'
	basename = (char*) g_malloc (blen);
	snprintf (basename, blen, ".%s.fsi", base);
	g_free (base);

	path = g_build_filename (dir_path, basename, NULL);

	g_free (dir_path);
	g_free (basename);

	FILE* f = fopen (path, "w");
	g_free (path);

	return f;
}

static FILE* vstfx_infofile_for_write (char* dllpath)
{
	FILE* f;

	if ((f = vstfx_infofile_create (dllpath, 0)) == NULL) {
		f = vstfx_infofile_create (dllpath, 1);
	}
	
	return f;
}

static int vstfx_can_midi(VSTFX *vstfx)
{
	struct AEffect *plugin = vstfx->plugin;
	
	int vst_version = plugin->dispatcher (plugin, effGetVstVersion, 0, 0, NULL, 0.0f);

	if (vst_version >= 2)
	{
		/* should we send it VST events (i.e. MIDI) */
		
		if ((plugin->flags & effFlagsIsSynth) || (plugin->dispatcher (plugin, effCanDo, 0, 0,(void*) "receiveVstEvents", 0.0f) > 0))
		    return -1;
	}
	return false;
}

static VSTFXInfo* vstfx_info_from_plugin(VSTFX *vstfx)
{

	VSTFXInfo* info = (VSTFXInfo*) malloc(sizeof(VSTFXInfo));
	
	struct AEffect *plugin;
	int i;
	
	/*We need to init the creator because some plugins
	  fail to implement getVendorString, and so won't stuff the
	  string with any name*/
	
	char creator[65] = "Unknown\0";
	
	if(!vstfx)
	{
		vstfx_error( "** ERROR ** VSTFXinfofile : vstfx ptr is NULL\n" );
		return NULL;
	}
	
	if(!info)
		return NULL;
	
	plugin = vstfx->plugin;
	
	info->name = strdup(vstfx->handle->name ); 
	
	/*If the plugin doesn't bother to implement GetVendorString we will
	  have pre-stuffed the string with 'Unkown' */
	
	plugin->dispatcher (plugin, effGetVendorString, 0, 0, creator, 0);
	
	/*Some plugins DO implement GetVendorString, but DON'T put a name in it
	  so if its just a zero length string we replace it with 'Unknown' */
	
	if (strlen(creator) == 0) {
		info->creator = strdup("Unknown");
	} else {
		info->creator = strdup (creator);
	}
	
	info->UniqueID = plugin->uniqueID;
	
	info->Category = strdup("None");          // FIXME:  
	info->numInputs = plugin->numInputs;
	info->numOutputs = plugin->numOutputs;
	info->numParams = plugin->numParams;
	info->wantMidi = vstfx_can_midi(vstfx); 
	info->hasEditor = plugin->flags & effFlagsHasEditor ? true : false;
	info->canProcessReplacing = plugin->flags & effFlagsCanReplacing ? true : false;
	info->ParamNames = (char **) malloc(sizeof(char*)*info->numParams);
	info->ParamLabels = (char **) malloc(sizeof(char*)*info->numParams);

	for(i=0; i < info->numParams; i++) {
		char name[64];
		char label[64];
		
		/*Not all plugins give parameters labels as well as names*/
		
		strcpy(name, "No Name");
		strcpy(label, "No Label");
		
		plugin->dispatcher (plugin, effGetParamName, i, 0, name, 0);
		info->ParamNames[i] = strdup(name);
		
		//NOTE: 'effGetParamLabel' is no longer defined in vestige headers
		//plugin->dispatcher (plugin, effGetParamLabel, i, 0, label, 0);
		info->ParamLabels[i] = strdup(label);
	}
	return info;
}

/* A simple 'dummy' audiomaster callback which should be ok,
we will only be instantiating the plugin in order to get its info*/

static intptr_t simple_master_callback(struct AEffect *, int32_t opcode, int32_t, intptr_t, void *, float)
{
	if (opcode == audioMasterVersion)
		return 2;
	else
		return 0;
}

/*Try to get plugin info - first by looking for a .fsi cache of the
data, and if that doesn't exist, load the plugin, get its data and
then cache it for future ref*/

VSTFXInfo *vstfx_get_info(char *dllpath)
{
	FILE* infofile;
	VSTFXHandle *h;
	VSTFX *vstfx;
	VSTFXInfo *info;

	if ((infofile = vstfx_infofile_for_read (dllpath)) != NULL) {
		VSTFXInfo *info;
		info = load_vstfx_info_file (infofile);
		fclose (infofile);
		return info;
	} 
	
	if(!(h = vstfx_load(dllpath)))
		return NULL;
	
	if(!(vstfx = vstfx_instantiate(h, simple_master_callback, NULL))) {
	    	vstfx_unload(h);
	    	vstfx_error( "** ERROR ** VSTFXinfofile : Instantiate failed\n" );
	    	return NULL;
	}
	
	infofile = vstfx_infofile_for_write (dllpath);
	
	if(!infofile) {
		vstfx_close(vstfx);
		vstfx_unload(h);
		vstfx_error("cannot create new FST info file for plugin");
		return NULL;
	}
	
	info = vstfx_info_from_plugin(vstfx);
	
	save_vstfx_info_file(info, infofile);
	fclose (infofile);
	
	vstfx_close(vstfx);
	vstfx_unload(h);
	
	return info;
}

void vstfx_free_info(VSTFXInfo *info )
{
    int i;

    for(i=0; i < info->numParams; i++)
	{
		free(info->ParamNames[i]);
		free(info->ParamLabels[i]);
    }
	
    free(info->name);
    free(info->creator);
    free(info->Category);
    free(info);
}


