#ifdef HAVE_CANBERRA
#include "ca_plugin.h"


bool (*ca_plug_init)(void);
bool (*ca_plug_termbell)(void);

bool ca_plug_load(void)
{
	void *handle;

	if (!(handle = dlopen("librcfvtca.so", RTLD_LAZY)))
		return false;
	if (!(ca_plug_init = dlsym(handle, "rcfvtca_init")))
		return false;
	if (!(ca_plug_termbell = dlsym(handle, "rcfvtca_termbell")))
		return false;
	return true;
}
#endif
