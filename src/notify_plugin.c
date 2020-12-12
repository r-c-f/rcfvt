#include "notify_plugin.h"


bool (*notify_plug_init)(void);
bool (*notify_plug_termbell)(void);

bool notify_plug_load(void)
{
	void *handle;

	if (!(handle = dlopen("librcfvtnotify.so", RTLD_LAZY)))
		goto error;
	if (!(notify_plug_init = dlsym(handle, "rcfvtnotify_init")))
		goto error;
	if (!(notify_plug_termbell = dlsym(handle, "rcfvtnotify_termbell")))
		goto error;
	return true;
error:
	g_warning("Could not load notify plugin: %s", dlerror());
	return false;
}
