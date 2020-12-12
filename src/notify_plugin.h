#ifndef NOTIFY_PLUG_H_INC
#define NOTIFY_PLUG_H_INC
#include <dlfcn.h>
#include <stdbool.h>
#include <gtk/gtk.h>

extern bool (*notify_plug_init)(void);
extern bool (*notify_plug_termbell)(const char *title);
extern bool notify_plug_load(void);

#endif
