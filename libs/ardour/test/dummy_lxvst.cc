/* Dummy LXVST methods so that libardour can be linked against the test code */

#include "ardour/vstfx.h"

int
vstfx_init (void* ptr)
{
	return 0;
}

VSTFX *
vstfx_instantiate (VSTHandle* fhandle, audioMasterCallback amc, void* userptr)
{
	return 0;
}

void
vstfx_close (VSTFX* vstfx)
{

}

VSTHandle *
vstfx_load (const char *path)
{
	return 0;
}

int
vstfx_unload (VSTHandle* fhandle)
{
	return -1;
}

void
vstfx_destroy_editor (VSTFX *)
{

}
