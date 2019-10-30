#include <stdbool.h>
#include <gtk/gtk.h>
#ifdef HAVE_CANBERRA
#include <canberra.h>
#endif

// Fallback options if not present in config file.
#define DEFAULT_FONT "Fixed 9"
#define DEFAULT_SHELL "/bin/bash"
#define DEFAULT_OPACITY 1.0
#define DEFAULT_SCROLLBACK 1000000
#define DEFAULT_SPAWN_TIMEOUT -1
#define DEFAULT_URL_REGEX "(ftp|http)s?://[-@a-zA-Z0-9.?$%&/=_~#.,:;+]*"
#define DEFAULT_FIFO_TIMEOUT 5

// Theme configuration
struct theme {
        GdkRGBA fg, bg;
        GdkRGBA colors[256];
	char *font;
        gboolean bold_is_bright;
	size_t size;
};

struct config {
	char *shell;
	double opacity;
	int scrollback;
	int spawn_timeout;
	bool select_to_clipboard;
	struct theme theme;
	GdkModifierType url_modifiers;
	char *url_regex;
	char *url_action;
	bool beep_bell;
	bool canberra_bell;
	bool url_osc8;
	bool url_spawn_sync;
	bool single_proc;
	char *fifo_path;
	int fifo_timeout;
#ifdef HAVE_CANBERRA
	ca_context *ca_con;
#endif
};



extern void conf_load(struct config *conf);

