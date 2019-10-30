#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "config.h"

/* read file into a buffer, resizing as needed
 *
 * Returns:
 *  0 on succes
 *  >0 on failure to open file
 *  <0 if allocation fails (inconsistent buffer state)
*/
static int buf_append_file(char **buf, size_t *len, size_t *pos, char *path)
{
	FILE *f;
	size_t read_count;
	if (!path)
		return 1;
	if (!(f = fopen(path, "r"))) {
		return 2;
	}
	while ((read_count = fread(*buf + *pos, 1, *len - *pos - 1, f))) {
		*pos += read_count;
		if (*len - *pos <= 2) {
			if (!(*buf = realloc(*buf, *len *= 2))) {
				fclose(f);
				return -1;
			}
		}
	}
	fclose(f);
	return 0;
}

/* get base path for configuration */
static char *get_base_path(void)
{
	char *conf_home = getenv("XDG_CONFIG_HOME");
	if (conf_home)
		return g_build_filename(conf_home, "rcfvt", NULL);
	return g_build_filename(getenv("HOME"), ".config", "rcfvt", NULL);
}

/* read in full configuration */
char *read_full_conf_buf(void)
{
	char *buf, *full_path;
	const char *filename;
	size_t len = 1024; //how many bytes are allocated
        size_t pos = 0; //index of next empty block of buf
        GError *err = NULL;
        GDir *dir;

	g_autofree gchar *base_path = get_base_path();
	g_autofree gchar *main_file = g_build_filename(base_path, "rcfvt.conf", NULL);
	g_autofree gchar *dirname = g_build_filename(base_path, "rcfvt.conf.d", NULL);

        if (!(buf = calloc(len,1))) {
                return NULL;
        }
        if (main_file) {
                if (buf_append_file(&buf, &len, &pos, main_file) < 0) {
                        free(buf);
                        return NULL;
                }
        }
        if (!dirname)
                return buf;
        if (!(dir = g_dir_open(dirname, 0, &err))) {
                g_warning("Could not open dir %s: %s", dirname, err->message);
                return buf;
        }
        while ((filename = g_dir_read_name(dir))) {
                if (!g_str_has_suffix(filename, ".conf"))
                        continue;
                full_path = g_build_filename(dirname, filename, NULL);
                if (buf_append_file(&buf, &len, &pos, full_path) < 0) {
                        g_free(full_path);
                        free(buf);
                        return NULL;
                }
                g_free(full_path);
        }
        g_dir_close(dir);
        buf[pos + 1] = '\0';
        return buf;
}

GdkModifierType gdk_mod_parse(char *name)
{
        if (!strcasecmp("shift", name))
                return GDK_SHIFT_MASK;
        if (!strcasecmp("control", name))
                return GDK_CONTROL_MASK;
        if (!strcasecmp("mod1", name))
                return GDK_MOD1_MASK;
        if (!strcasecmp("mod2", name))
                return GDK_MOD2_MASK;
        if (!strcasecmp("mod3", name))
                return GDK_MOD3_MASK;
        if (!strcasecmp("mod4", name))
                return GDK_MOD4_MASK;
        if (!strcasecmp("mod5", name))
                return GDK_MOD5_MASK;
        if (!strcasecmp("super", name))
                return GDK_SUPER_MASK;
        if (!strcasecmp("hyper", name))
                return GDK_HYPER_MASK;
        if (!strcasecmp("meta", name))
                return GDK_META_MASK;
        return 0;
}

int keyfile_load_color(GdkRGBA *dest, GKeyFile *kf, char* group, char *key)
{
        int ret = 1;
        char *val = g_key_file_get_string(kf, group, key, NULL);
        if (val && gdk_rgba_parse(dest,val)) {
                ret = 0;
        }
        g_free(val);
        return ret;
}

// Set size of a theme from GKeyFile configuration
size_t conf_theme_set_size(struct theme *theme, GKeyFile *conf)
{
        char *val, **size;
        char *sizes[] = {"0", "8", "16", "232", "256", NULL}; // all VTE supports.
        for (size = sizes; *size; ++size) {
                val = g_key_file_get_string(conf, "theme", *size, NULL);
                if (!val) {
                        break;
                } else {
                        g_free(val);
                }
        }
        theme->size = atol(*size);
        g_free(val);
        return theme->size;
}

// Load whole theme from GKeyFile configuration
int conf_load_theme(struct theme *theme, GKeyFile *conf)
{
        char key[4];
        size_t i;
        int missing = 0;
        conf_theme_set_size(theme, conf);
        for (i = 0; i < theme->size; ++i) {
                snprintf(key, 4, "%zd", i);
                missing += keyfile_load_color(theme->colors + i, conf, "theme", key);
        }
        missing += keyfile_load_color(&(theme->fg), conf, "theme", "fg");
        missing += keyfile_load_color(&(theme->bg), conf, "theme", "bg");
        theme->bold_is_bright = g_key_file_get_boolean(conf, "theme", "bold_is_bright", NULL);
        return missing;
}
