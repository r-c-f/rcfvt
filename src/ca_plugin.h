#ifndef CA_PLUG_H_INC
#define CA_PLUG_H_INC
#include <dlfcn.h>
#include <stdbool.h>
#include <gtk/gtk.h>

extern bool (*ca_plug_init)(void);
extern bool (*ca_plug_termbell)(void);
extern bool ca_plug_load(void);

#endif
