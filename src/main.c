#include <stdlib.h>
#include <unistd.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "config.h"
#ifdef HAVE_CANBERRA
#include <canberra.h>
#endif

// Config path.
#define DEFAULT_CONF_DIR ".config/rcfvt/"

// Fallback options if not present in config file.
#define DEFAULT_FONT "Fixed 9"
#define DEFAULT_SHELL "/bin/bash"
#define DEFAULT_OPACITY 1.0
#define DEFAULT_SCROLLBACK 1000000
#define DEFAULT_SPAWN_TIMEOUT -1
#define DEFAULT_URL_REGEX "(ftp|http)s?://[-@a-zA-Z0-9.?$%&/=_~#.,:;+]*"

// Commands for URL stuff, and a static command buffer
static char *url_action;
static char *cmd_buf;
static size_t cmd_buf_len;
static GdkModifierType url_modifiers = 0;
// Sound stuff
#ifdef HAVE_CANBERRA
static ca_context *cabell;
#endif


// update_visuals updates the video display format of the screen for the given
// window.
void update_visuals(GtkWidget *win) {
	GdkScreen *screen = gtk_widget_get_screen(win);
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	gtk_widget_set_visual(win, visual);
}

// clear_shell clears the given shell window.
void clear_shell(VteTerminal *vte) {
	vte_terminal_reset(vte, TRUE, TRUE);
	VtePty *pty = vte_terminal_get_pty(vte);
	write(vte_pty_get_fd(pty), "\x0C", 1);
}

// on_select_clipboard will copy the selected text to clipboard
void on_select_clipboard(VteTerminal *vte, gpointer data)
{
	if (vte_terminal_get_has_selection(vte)) {
		vte_terminal_copy_clipboard_format(vte, VTE_FORMAT_TEXT);
	}
}

// on_shell_spawn handles the spawn of the child shell process
void on_shell_spawn(VteTerminal *vte, GPid pid, GError *error, gpointer user_data)
{
	if (error) {
		g_error("error spawning shell: %s\n", error->message);
	}
}

//handle button presses
gboolean on_button_press(GtkWidget *win, GdkEventButton *event, VteTerminal *vte)
{
	GtkClipboard *cb;
	char *link;
	GdkModifierType mod;
	GError *err = NULL;

	mod = event->state & gtk_accelerator_get_default_mod_mask();

	link = vte_terminal_hyperlink_check_event(vte, (GdkEvent*)event);
	if (!link) //Try for regex
		link = vte_terminal_match_check_event(vte, (GdkEvent*)event, NULL);
	if (!link) //we done here
		return FALSE;

	if (event->button == 1) { //run action on left click
		if ((mod & url_modifiers) == url_modifiers) {
			if (url_action) {
				snprintf(cmd_buf, cmd_buf_len, "%s '%s'", url_action, link);
				if (!g_spawn_command_line_async(cmd_buf, &err))
					g_warning("Command spawn failed: %s", err->message);
			}
		}
	}else if (event->button == 3) { //copy on right click
		cb = gtk_clipboard_get(GDK_NONE);
		gtk_clipboard_set_text(cb, link, -1);
	}
	return FALSE;
}

// on_key_press handles key-press events for the GTK window.
gboolean on_key_press(GtkWidget *win, GdkEventKey *event, VteTerminal *vte) {
	// [ctrl] + [shift]
	GdkModifierType mod = event->state & gtk_accelerator_get_default_mod_mask();
	if (mod == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
		switch (event->keyval) {
		// [ctrl] + [shift] + 'c'
		case GDK_KEY_C:
		case GDK_KEY_c:
			vte_terminal_copy_clipboard_format(vte, VTE_FORMAT_TEXT);
			return TRUE;
		// [ctrl] + [shift] + 'v'
		case GDK_KEY_V:
		case GDK_KEY_v:
			vte_terminal_paste_clipboard(vte);
			return TRUE;
		// [ctrl] + [shift] + 'l'
		case GDK_KEY_L:
		case GDK_KEY_l:
			clear_shell(vte);
			return TRUE;
		}
	}

	// [ctrl]
	double font_scale;
	if ((mod&GDK_CONTROL_MASK) != 0) {
		switch (event->keyval) {
		// [ctrl] + '+'
		case GDK_KEY_plus:
			font_scale = vte_terminal_get_font_scale(vte);
			font_scale *= 1.1;
			vte_terminal_set_font_scale(vte, font_scale);
			return TRUE;
		// [ctrl] + '-'
		case GDK_KEY_minus:
			font_scale = vte_terminal_get_font_scale(vte);
			font_scale /= 1.1;
			vte_terminal_set_font_scale(vte, font_scale);
			return TRUE;
		}
	}
	return FALSE;
}

// on_screen_change handles screen-changed events for the GTK window.
void on_screen_change(GtkWidget *win, GdkScreen *prev, gpointer data) {
	update_visuals(win);
}

// set urgent when bell rings.
void on_bell(VteTerminal *vte, gpointer data)
{
	if (!gtk_window_has_toplevel_focus(GTK_WINDOW(data)))
		gtk_window_set_urgency_hint(GTK_WINDOW(data), TRUE);
#ifdef HAVE_CANBERRA
	if (cabell)
		ca_context_play(cabell, 0, CA_PROP_EVENT_ID, "bell-terminal", NULL);
#endif
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	struct theme *theme = NULL;
	char *font, *shell;
	double opacity;
	int scrollback, spawn_timeout;
	gboolean select_to_clipboard, audible_bell = TRUE;
	VteRegex *urlre;
	char *url_regex = DEFAULT_URL_REGEX;
	//get default shell
	char *default_shell = vte_get_user_shell();
	if (!default_shell)
		default_shell = DEFAULT_SHELL;

	// Parse config file.
	GError *err = NULL;
	char *conf_buf = read_full_conf_buf();
	if (!conf_buf) {
		g_error("Configuration could not be read");
	}
	GKeyFile *conf = g_key_file_new();
	g_key_file_load_from_data(conf, conf_buf, (gsize)-1, G_KEY_FILE_NONE, NULL);
	font = g_key_file_get_string(conf, "main", "font", &err);
	if (err)
		font = DEFAULT_FONT;
	err = NULL;
	shell = g_key_file_get_string(conf, "main", "shell", &err);
	if (err)
		shell = default_shell;
	err = NULL;
	opacity = g_key_file_get_double(conf, "main", "opacity", &err);
	if (err)
		opacity = DEFAULT_OPACITY;
	err = NULL;
	scrollback = g_key_file_get_integer(conf, "main", "scrollback", &err);
	if (err)
		scrollback = DEFAULT_SCROLLBACK;
	err = NULL;
	spawn_timeout = g_key_file_get_integer(conf, "main", "spawn_timeout", &err);
	if (err)
		spawn_timeout = DEFAULT_SPAWN_TIMEOUT;
	err = NULL;
	select_to_clipboard = g_key_file_get_boolean(conf, "main", "select_to_clipboard", &err);
	if (err)
		select_to_clipboard = FALSE;
	err = NULL;
	if (g_key_file_has_group(conf, "theme")) {
		if (!(theme = calloc(1, sizeof(struct theme)))) {
			g_warning("Could not allocate theme structure.");
		} else if (conf_load_theme(theme, conf)) {
			g_warning("Could not load complete theme; using default colors");
			free(theme);
			theme = NULL;
		}
	}
	if (g_key_file_has_group(conf, "url")) {
		char *mod_names = g_key_file_get_string(conf, "url", "modifiers", &err);
		if (!err) {
			char *mod_name = strtok(mod_names, "|+");
			do {
				url_modifiers |= gdk_mod_parse(mod_name);
			} while ((mod_name = strtok(NULL, "|+")));
		}
		err = NULL;
		url_regex = g_key_file_get_string(conf, "url", "regex", &err);
		if (err)
			url_regex = DEFAULT_URL_REGEX;
		err = NULL;
		url_action = g_key_file_get_string(conf, "url", "action", &err);
		if (err)
			url_action = NULL;
		err = NULL;
	}
	if (g_key_file_has_group(conf, "sound")) {
		if (!g_key_file_get_boolean(conf, "sound", "audible_bell", &err)) {
			if (!err) {
				audible_bell = FALSE;
			}
		}
		err = NULL;
#ifdef HAVE_CANBERRA
		if (g_key_file_get_boolean(conf, "sound", "canberra_bell", &err)) {
			int ret;
			if ((ret = ca_context_create(&cabell))) {
				g_warning("Could not create canberra context: %s", ca_strerror(ret));
				cabell = NULL;
			}
			if (cabell && (ret = ca_context_open(cabell))) {
				g_warning("Could not open canberra context: %s", ca_strerror(ret));
				ca_context_destroy(cabell);
				cabell = NULL;
			}
		}
		err = NULL;
#endif
	}

	g_key_file_free(conf);
	free(conf_buf);

	// Create window with a terminal emulator.
	GtkWindow *win = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	VteTerminal *vte = VTE_TERMINAL(vte_terminal_new());
	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(vte));
	vte_terminal_set_font(vte, pango_font_description_from_string(font));
	vte_terminal_set_scrollback_lines(vte, scrollback);

	// Connect signals.
	g_signal_connect(win, "delete_event", gtk_main_quit, NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), vte);
	g_signal_connect(vte, "child-exited", gtk_main_quit, NULL);
	g_signal_connect(vte, "bell", G_CALLBACK(on_bell), win);
	g_signal_connect(vte, "button-press-event", G_CALLBACK(on_button_press), vte);
	if (select_to_clipboard)
		g_signal_connect(vte, "selection-changed", G_CALLBACK(on_select_clipboard), vte);
	// Enable transparency.
	if (opacity < 1) {
		GdkScreen *screen = gdk_screen_get_default();
		if (!gdk_screen_is_composited(screen)) {
			fprintf(stderr, "unable to enable transparency; no compositing manager running (e.g. xcompmgr)\n.");
		} else {
			g_signal_connect(win, "screen-changed", G_CALLBACK(on_screen_change), NULL);
			update_visuals(GTK_WIDGET(win));
			gtk_widget_set_app_paintable(GTK_WIDGET(win), TRUE);
			gtk_widget_set_opacity(GTK_WIDGET(win), opacity);
		}
	}

	//Set theme.
	if (theme) {
		vte_terminal_set_colors(vte, &(theme->fg), &(theme->bg), theme->colors, theme->size);
		vte_terminal_set_bold_is_bright(vte, theme->bold_is_bright);
	}
	free(theme);
	//Disable bell if requested.
	if (!audible_bell)
		vte_terminal_set_audible_bell(vte, FALSE);

	//Allow links
	vte_terminal_set_allow_hyperlink(vte, TRUE);

	// Fork shell process.
	argv[0] = shell;
#if (VTE_MAJOR_VERSION < 1) && (VTE_MINOR_VERSION < 52)
	//FIX for https://gitlab.gnome.org/GNOME/vte/issues/7
	vte_terminal_spawn_sync(vte, 0, NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, NULL);
#else
	//proper way.
	vte_terminal_spawn_async(vte, 0, NULL, argv, NULL, 0, NULL, NULL, NULL, spawn_timeout, NULL, &on_shell_spawn, (gpointer)win);
#endif
	//setup URL regex
	urlre = url_regex ? vte_regex_new_for_match(url_regex, -1, PCRE2_CASELESS | PCRE2_MULTILINE, &err) : NULL;
	if (urlre) {
		if (!vte_regex_jit(urlre, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT | PCRE2_JIT_PARTIAL_HARD, &err)) { // vte source does not rule out HARD in the future, so we do it anyway.
			g_warning("URL regex JIT failed: %s", err->message);
		}
		vte_terminal_match_add_regex(vte, urlre, PCRE2_NOTEMPTY);
	}
	// Set up command buffer
	if (url_action) {
		cmd_buf_len = sysconf(_SC_ARG_MAX);
		if (!(cmd_buf = malloc(cmd_buf_len))) {
			url_action = NULL;
			cmd_buf_len = 0;
		}
	}

	// Show main window.
	gtk_widget_show_all(GTK_WIDGET(win));
	gtk_main();

	return 0;
}


