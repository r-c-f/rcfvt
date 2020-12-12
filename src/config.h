#include <stdbool.h>
#include <gtk/gtk.h>

// Fallback options if not present in config file.
#define DEFAULT_FONT "Misc Fixed 9"
#define DEFAULT_SHELL "/bin/bash"
#define DEFAULT_OPACITY 1.0
#define DEFAULT_SCROLLBACK 1000000
#define DEFAULT_SPAWN_TIMEOUT -1
#define DEFAULT_URL_REGEX "(ftp|http)s?://[-@a-zA-Z0-9.?$%&/=_~#.,:;+]*"
#define DEFAULT_FIFO_TIMEOUT 5


// Theme configuration
#define THEME_SIZE_MAX 256
struct theme {
	double opacity;
        GdkRGBA fg, bg;
	bool fg_set, bg_set;
        GdkRGBA colors[THEME_SIZE_MAX];
	char *font;
        bool bold_is_bright;
	size_t size;
};

struct config {
	char *shell;
	int scrollback;
	int spawn_timeout;
	bool select_to_clipboard;
	struct theme theme;
	GdkModifierType url_modifiers;
	char *url_regex;
	char *url_action;
	bool beep_bell;
	bool canberra_bell;
	bool notify_bell;
	bool notify_bell_x11;
	bool notify_bell_wl;
	bool url_osc8;
	bool url_spawn_sync;
	bool single_proc;
	char *fifo_path;
	int fifo_timeout;
};

enum conf_backend {
	conf_backend_unknown=-1,
	conf_backend_x11,
	conf_backend_wl
};

extern void conf_load(struct config *conf);

