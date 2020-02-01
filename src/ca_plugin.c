#ifdef HAVE_CANBERRA
#include "ca_plugin.h"


bool (*ca_plug_init)(void);
bool (*ca_plug_termbell)(void);

bool ca_plug_load(void)
{
	void *handle;

	if (!(handle = dlopen("librcfvtca.so", RTLD_LAZY)))
		goto error;
	if (!(ca_plug_init = dlsym(handle, "rcfvtca_init")))
		goto error;
	if (!(ca_plug_termbell = dlsym(handle, "rcfvtca_termbell")))
		goto error;
	return true;
error:
	g_warning("Could not load canberra plugin: %s", dlerror());
	return false;
}
#endif
