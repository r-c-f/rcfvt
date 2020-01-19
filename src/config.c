#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include "config.h"

/* read file into a buffer, resizing as needed */
static bool buf_append_file(char **buf, size_t *len, size_t *pos, char *path)
{
	FILE *f;
	size_t read_count;
	if (!path)
		return false;
	if (!(f = fopen(path, "r"))) {
		return false;
	}
	while ((read_count = fread(*buf + *pos, 1, *len - *pos - 1, f))) {
		*pos += read_count;
		if (*len - *pos <= 2) {
			*buf = g_realloc(*buf, *len *= 2);
		}
	}
	fclose(f);
	return true;
}

/* get base path for configuration */
static inline char *get_base_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "rcfvt", NULL);
}

/* read in full configuration */
static char *read_full_conf_buf(void)
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

        buf = g_malloc0(len);
        if (main_file) {
                if (!buf_append_file(&buf, &len, &pos, main_file)) {
                        g_free(buf);
                        return NULL;
                }
        }
        if (!dirname)
                goto done;
        if (!(dir = g_dir_open(dirname, 0, &err))) {
                g_info("Could not open config dir %s: %s", dirname, err->message);
                goto done;
        }
        while ((filename = g_dir_read_name(dir))) {
                if (!g_str_has_suffix(filename, ".conf"))
                        continue;
                full_path = g_build_filename(dirname, filename, NULL);
                if (!buf_append_file(&buf, &len, &pos, full_path)) {
                        g_free(full_path);
                        g_free(buf);
                        return NULL;
                }
                g_free(full_path);
        }
        g_dir_close(dir);
done:
        buf[pos + 1] = '\0';
        return buf;
}

/*
 * tries to use g_key_file_get_typ() to set dest, if it fails, 
 * sets dest to def instead
 */
#define KEYFILE_TRY_GET(kf, grp, key, dest, def) \
	do { \
		if (!kf) { \
			dest = def; \
		} else { \
			GError *err = NULL; \
			dest = _Generic((dest), bool: g_key_file_get_boolean, int: g_key_file_get_integer, char *: g_key_file_get_string, double: g_key_file_get_double)(kf, grp, key, &err); \
			if (err) \
				dest = def; \
		} \
	} while(0)



static GdkModifierType gdk_mod_parse(char *name)
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

/* Try to load a color from a keyfile. Returns 0 on success, 1 on failure */
static int keyfile_load_color(GdkRGBA *dest, GKeyFile *kf, char* group, char *key)
{
        int ret = 1;
	if (kf) {
		char *val = g_key_file_get_string(kf, group, key, NULL);
		if (val && gdk_rgba_parse(dest,val)) {
			ret = 0;
		}
		g_free(val);
	}
        return ret;
}

// Set size of a theme from GKeyFile configuration
static size_t conf_theme_set_size(struct theme *theme, GKeyFile *conf)
{
        char *val, **size;
        char *sizes[] = {"0", "8", "16", "232", "256", NULL}; // all VTE supports.
	if (!conf) {
		theme->size = 0;
		return theme->size;
	}
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
	assert(theme->size <= THEME_SIZE_MAX);
        return theme->size;
}

// Load whole theme from GKeyFile configuration
static bool conf_load_theme(struct theme *theme, GKeyFile *conf)
{
        size_t i;
        int missing = 0;
	KEYFILE_TRY_GET(conf, "theme", "font", theme->font, DEFAULT_FONT);
	KEYFILE_TRY_GET(conf, "theme", "opacity", theme->opacity, DEFAULT_OPACITY);
	KEYFILE_TRY_GET(conf, "theme", "bold_is_bright", theme->bold_is_bright, false);
        conf_theme_set_size(theme, conf);
	char key[1 + snprintf(NULL, 0, "%zd", theme->size)];
        for (i = 0; i < theme->size; ++i) {
                snprintf(key, sizeof(key), "%zd", i);
                missing += keyfile_load_color(theme->colors + i, conf, "theme", key);
        }
        missing += keyfile_load_color(&(theme->fg), conf, "theme", "fg");
        missing += keyfile_load_color(&(theme->bg), conf, "theme", "bg");
        return !missing;
}

void conf_load(struct config *conf)
{
	char *mod_names, *mod_name;
	GKeyFile *kf;

	char *default_shell = vte_get_user_shell();
	if (!default_shell)
		default_shell = DEFAULT_SHELL;
	char *conf_buf = read_full_conf_buf();
	if (conf_buf) {
		kf = g_key_file_new();
		g_key_file_load_from_data(kf, conf_buf, (gsize)-1, G_KEY_FILE_NONE, NULL);
	} else {
		g_warning("Could not read configuration file, using defaults");
		kf = NULL;
	}

	KEYFILE_TRY_GET(kf, "main", "shell", conf->shell,default_shell); 
	KEYFILE_TRY_GET(kf, "main", "scrollback", conf->scrollback, DEFAULT_SCROLLBACK);
	KEYFILE_TRY_GET(kf, "main", "spawn_timeout", conf->spawn_timeout, DEFAULT_SPAWN_TIMEOUT);
	KEYFILE_TRY_GET(kf, "main", "select_to_clipboard", conf->select_to_clipboard, false);
	KEYFILE_TRY_GET(kf, "main", "single_proc",  conf->single_proc, false);
	KEYFILE_TRY_GET(kf, "main", "fifo_path", conf->fifo_path, NULL);
	if (!conf->fifo_path) {
                conf->fifo_path = g_build_filename(g_get_user_runtime_dir(), "rcfvt_fifo", NULL);
	}
	KEYFILE_TRY_GET(kf, "main", "fifo_timeout", conf->fifo_timeout, DEFAULT_FIFO_TIMEOUT);
	if (!conf_load_theme(&(conf->theme), kf)) {
		g_warning("Could not load complete theme; using defaults");
		conf->theme.size = 0;
	}

	KEYFILE_TRY_GET(kf, "url", "modifiers", mod_names, NULL);
	if (mod_names) {
		mod_name = strtok(mod_names, "|+");
		do {
			conf->url_modifiers |= gdk_mod_parse(mod_name);
		} while ((mod_name = strtok(NULL, "|+")));
	}
	KEYFILE_TRY_GET(kf, "url", "regex", conf->url_regex, DEFAULT_URL_REGEX);
	KEYFILE_TRY_GET(kf, "url", "osc8", conf->url_osc8, false);
	KEYFILE_TRY_GET(kf, "url", "spawn_sync", conf->url_spawn_sync, false);
	KEYFILE_TRY_GET(kf, "url", "action", conf->url_action, NULL);

	KEYFILE_TRY_GET(kf, "sound", "beep_bell", conf->beep_bell, false);
#ifdef HAVE_CANBERRA
	bool canberra_bell;
	KEYFILE_TRY_GET(kf, "sound", "canberra_bell", canberra_bell, false);
	if (canberra_bell) {
		int ret;
		if ((ret = ca_context_create(&(conf->ca_con)))) {
			g_warning("Could not create canberra context: %s", ca_strerror(ret));
			conf->ca_con = NULL;
		}
		if (conf->ca_con && (ret = ca_context_open(conf->ca_con))) {
			g_warning("Could not open canberra context: %s", ca_strerror(ret));
			ca_context_destroy(conf->ca_con);
			conf->ca_con = NULL;
		}
	}
#endif
	if (kf) {
		g_key_file_free(kf);
		g_free(conf_buf);
	}
}


