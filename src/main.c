#include <stdlib.h>
#include <unistd.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "config.h"
#include "ipc.h"
#include "sopt.h"
#ifdef HAVE_CANBERRA
#include <canberra.h>
#endif

// Commands for URL stuff, and a static command buffer
static char *cmd_buf;
static size_t cmd_buf_len;
static VteRegex *urlre;

//Configuration
static struct config *conf;

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
		g_warning("error spawning shell: %s\n", error->message);
		g_signal_emit_by_name(vte, "child-exited");
	}
	//clean up argv
	g_strfreev(user_data);
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
		if ((mod & conf->url_modifiers) == conf->url_modifiers) {
			if (conf->url_action) {
				snprintf(cmd_buf, cmd_buf_len, "%s '%s'", conf->url_action, link);
				if (conf->url_spawn_sync) {
					if (system(cmd_buf) == -1)
						g_warning("Could not spawn URL handler: %s", strerror(errno));
				} else {
					if (!g_spawn_command_line_async(cmd_buf, &err))
						g_warning("Could not spawn URL handler: %s", err->message);
				}
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
	if (conf->ca_con)
		ca_context_play(conf->ca_con, 0, CA_PROP_EVENT_ID, "bell-terminal", NULL);
#endif
}

struct term {
	GtkWindow *win;
	VteTerminal *vte;
};


// close terminal window
void term_exit(VteTerminal *vte, gint status, gpointer user_data)
{
	GSList **terms = user_data;
	GSList *tl = *terms;
	if (!tl) {
		g_error("term list error");
	}
	struct term *t;
	while (tl) {
		t = tl->data;
		if (t->vte == vte)
			break;
		tl = tl->next;
	}
	gtk_widget_destroy(GTK_WIDGET(t->win));
	//gtk_widget_destroy is recursive for containers, so the vte instance
	//is handled properly here.
	*terms = g_slist_remove(*terms, t);
	if (!(*terms))
		gtk_main_quit();
}

// add new terminal window
bool term_start(GSList **l, char **argv)
{
	//build argv for spawned shell
	if (!argv) {
		argv = g_new(char *, 2);
		argv[0] = g_strdup(conf->shell);
		argv[1] = NULL;
	} else {
		argv = g_strdupv(argv); // simplifies cleanup -- allows free always
	}

	//create terminal and such
	struct term *t = g_malloc(sizeof(struct term));
	*l = g_slist_append(*l, t);
	t->win = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	t->vte = VTE_TERMINAL(vte_terminal_new());
	gtk_container_add(GTK_CONTAINER(t->win), GTK_WIDGET(t->vte));

	//Connect standard signals
	g_signal_connect(t->win, "key-press-event", G_CALLBACK(on_key_press), t->vte);
	g_signal_connect(t->vte, "child-exited", G_CALLBACK(term_exit), l);
	g_signal_connect(t->vte, "bell", G_CALLBACK(on_bell), t->win);
	g_signal_connect(t->vte, "button-press-event", G_CALLBACK(on_button_press), t->vte);
	//Configuration signals
	if (conf->select_to_clipboard)
		g_signal_connect(t->vte, "selection-changed", G_CALLBACK(on_select_clipboard), t->vte);
	//Add regex
	if (urlre)
		vte_terminal_match_add_regex(t->vte, urlre, PCRE2_NOTEMPTY);
	//Set theme
	vte_terminal_set_font(t->vte, pango_font_description_from_string(conf->theme.font));
	if (conf->theme.size) {
		vte_terminal_set_colors(t->vte, &(conf->theme.fg), &(conf->theme.bg), conf->theme.colors, conf->theme.size);
		vte_terminal_set_bold_is_bright(t->vte, conf->theme.bold_is_bright);
	}
	//Set other options
	vte_terminal_set_audible_bell(t->vte, conf->beep_bell);
	vte_terminal_set_allow_hyperlink(t->vte, conf->url_osc8);
	//Spawn shell
#if (VTE_MAJOR_VERSION < 1) && (VTE_MINOR_VERSION < 52)
        //FIX for https://gitlab.gnome.org/GNOME/vte/issues/7
        vte_terminal_spawn_sync(t->vte, 0, NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, NULL);
	g_strfreev(argv);
#else
        //proper way.
        vte_terminal_spawn_async(t->vte, 0, NULL, argv, NULL, 0, NULL, NULL, NULL, conf->spawn_timeout, NULL, &on_shell_spawn, argv);
#endif
	//Show the window and add to list
	gtk_widget_show_all(GTK_WIDGET(t->win));

	return true;
}

static struct sopt optspec[] = {
	SOPT_INIT_FLAGL('h', "help", "this message"),
	SOPT_INIT_FLAGL('s', "separate", "Run separate instance, even in single process mode"),
	SOPT_INIT_PARAML('f', "font", "font", "Font and size to use, overriding configuration"),
	SOPT_INIT_PARAML('F', "fifo", "fifo", "In single-process mode use fifo as path to control FIFO"),
	SOPT_INIT_AFTER("[command]", "command to run instead of shell"),
	SOPT_INIT_END
};

int main(int argc, char **argv)
{
	int opt, optind = 0, optpos = 0;
	char **sh_argv = NULL, *optarg;
	int sh_argc = 0;

	GError *err;
	GSList *terms = NULL;

	gtk_init(&argc, &argv);

	conf = g_malloc0(sizeof(struct config));
	conf_load(conf);

	sopt_usage_set(optspec, argv[0], "my custom terminal emulator");
	while ((opt = sopt_getopt(argc, argv, optspec, &optpos, &optind, &optarg)) != -1) {
		switch(opt) {
			case 's':
				conf->single_proc = false;
				break;
			case 'F':
				g_free(conf->fifo_path);
				conf->fifo_path = strdup(optarg);
				break;
			case 'f':
				g_free(conf->theme.font);
				conf->theme.font = strdup(optarg);
				break;
			case 'h':
				sopt_usage_s();
				return EXIT_SUCCESS;
			default:
				sopt_usage_s();
				return EXIT_FAILURE;

		}
	}
	//deal with custom commands
	if ((sh_argc = argc - optind))
		sh_argv = argv + optind;

	//try to start a client, if we're using single-process
	if (conf->single_proc) {
		if (client_start(conf->fifo_timeout, conf->fifo_path, sh_argc, sh_argv)) {
			exit(0);
		} else {
			g_warning("Could not connect to existing instance FIFO");
		}
	}

	//setup URL regex
	err = NULL;
	urlre = conf->url_regex ? vte_regex_new_for_match(conf->url_regex, -1, PCRE2_CASELESS | PCRE2_MULTILINE, &err) : NULL;
	if (urlre) {
		if (!vte_regex_jit(urlre, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT | PCRE2_JIT_PARTIAL_HARD, &err)) { // vte source does not rule out HARD in the future, so we do it anyway.
			g_warning("URL regex JIT failed: %s", err->message);
		}
	}
	// Set up command buffer
	if (conf->url_action) {
		cmd_buf_len = sysconf(_SC_ARG_MAX);
		cmd_buf = g_malloc(cmd_buf_len);
	}
	// Start main instance, server (if applicable), main loop
	term_start(&terms, sh_argv);
	if (conf->single_proc) {
		if (!server_start(conf->fifo_path, &terms))
			g_warning("Could not start server for single process mode");
	}
	gtk_main();
	unlink(conf->fifo_path);
	return 0;
}

