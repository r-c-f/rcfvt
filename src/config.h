#include <gtk/gtk.h>


// Theme configuration
struct theme {
        GdkRGBA fg, bg;
        GdkRGBA colors[256];
        gboolean bold_is_bright;
        size_t size;
};

extern int conf_load_theme(struct theme *theme, GKeyFile *conf);
extern char *read_full_conf_buf(void);
extern GdkModifierType gdk_mod_parse(char *name);
