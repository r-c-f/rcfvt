#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <glib.h>
#include <sys/stat.h>
#include "ipc.h"
#include "fdio_full.h"

//forward declaration for term_start
extern bool term_start(GSList **l, char **argv);

/* write argv array */
bool argv_write(int fd, int argc, char **argv)
{
	size_t arglen;
	char *empty_argv[] = {NULL};
	char nul = '\0';
	// allow for 0 argc -- i.e. we calculate it ourselves
	if (!argc) {
		if (argv) {
			char **arg = argv;
			for (argc = 0; *arg; ++argv);
		} else {
			//allow for empty argv
			argv = empty_argv;
		}
	}

	if (!write_full(fd, &argc, sizeof(argc)))
		return false;
	while (*argv) {
		arglen = strlen(*argv);
		if (!write_full(fd, &arglen, sizeof(arglen)))
			return false;
		if (!write_full(fd, *argv, arglen))
			return false;
		if (!write_full(fd, &nul, 1))
			return false;
		++argv;
	}
	return true;
}

/* read argv array */
char **argv_read(int fd)
{
	int argc, i;
	size_t arglen;
	char **argv;

	if (!read_full(fd, &argc, sizeof(argc)))
		return NULL;
	if (!argc)
		return NULL; //not an error - just empty.
	//g_new0 lets us skip setting everything to NULL here.
	argv = g_new0(char *, argc + 1); //argc + 1 for NULL termination

	for (i = 0; i < argc; ++i) {
		if (!read_full(fd, &arglen, sizeof(arglen)))
			goto argv_read_err;
		argv[i] = g_malloc(arglen + 1);
		if (!read_full(fd, argv[i], arglen + 1))
			goto argv_read_err;
		if (argv[i][arglen] != '\0')
			goto argv_read_err; //we've lost sync
	}
	return argv;
argv_read_err:
	g_strfreev(argv);
	return NULL;
}

static void sig_hand(int sig)
{
        if (sig == SIGALRM) {
                g_info("Stale FIFO detected");
        }
}

//Attempt to connect to existing instance and launch a shell there
bool client_start(int timeout, const char *fifo_path, int argc, char **argv)
{
	bool ret = true;
        struct stat sbuf;
        struct sigaction act;
        int fifo;

        if (stat(fifo_path, &sbuf))
                return false;
	if (!S_ISFIFO(sbuf.st_mode)) {
		g_warning("%s is not a FIFO", fifo_path);
		return false;
	}

        //for timeout purposes -- we want to remove stale FIFOs and try again
	memset(&act, 0, sizeof(act));
        act.sa_handler = sig_hand;
	sigemptyset(&act.sa_mask);
        act.sa_flags &= ~SA_RESTART;
        sigaction(SIGALRM, &act, NULL);
        alarm(timeout); //ought to be reasonable, maybe...
        fifo = open(fifo_path, O_WRONLY);
	alarm(0);
        if (fifo == -1)
                return false;
	lockf(fifo, F_LOCK, 0);

        if (!argv_write(fifo, argc, argv))
		ret = false;

        lockf(fifo, F_ULOCK, 0);
        close(fifo);
        return ret;
}

//Event for new FIFO data
gboolean on_fifo_data(GIOChannel *source, GIOCondition condition, gpointer data)
{
	int fifo_fd;
        char **argv = NULL;

	fifo_fd = g_io_channel_unix_get_fd(source);
	argv = argv_read(fifo_fd);

        if (!term_start(data, argv)) {
                g_warning("could not start terminal from fifo");
        	g_strfreev(argv);
	}
        return true;
}

//Set up server
bool server_start(char *fifo_path, GSList **terms)
{
	GIOChannel *fifo;
	int fifo_fd;

	unlink(fifo_path);
	if (mkfifo(fifo_path, 0600))
		return false;
	if ((fifo_fd = open(fifo_path, O_RDONLY | O_NONBLOCK)) == -1) {
		unlink(fifo_path);
		return false;
	}
	fifo = g_io_channel_unix_new(fifo_fd);
	g_io_add_watch(fifo, G_IO_IN, on_fifo_data, terms);

	return true;
}
