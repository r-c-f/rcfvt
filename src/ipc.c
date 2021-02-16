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

bool fifo_replaced = false;

/* write argv array */
bool argv_write(int fd, int argc, char **argv)
{
	size_t arglen;
	char nul = '\0';
	if (!write_full(fd, &argc, sizeof(argc), 0))
		return false;
	if (!argv)
		return true;
	while (*argv) {
		arglen = strlen(*argv);
		if (!write_full(fd, &arglen, sizeof(arglen), 0))
			return false;
		if (!write_full(fd, *argv, arglen, 0))
			return false;
		if (!write_full(fd, &nul, 1, 0))
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

	if (!read_full(fd, &argc, sizeof(argc), 0))
		return NULL;
	if (!argc)
		return NULL; //not an error - just empty.
	//g_new0 lets us skip setting everything to NULL here.
	argv = g_new0(char *, argc + 1); //argc + 1 for NULL termination

	for (i = 0; i < argc; ++i) {
		if (!read_full(fd, &arglen, sizeof(arglen), 0))
			goto argv_read_err;
		argv[i] = g_malloc(arglen + 1);
		if (!read_full(fd, argv[i], arglen + 1, 0))
			goto argv_read_err;
		if (argv[i][arglen] != '\0')
			goto argv_read_err; //we've lost sync
	}
	return argv;
argv_read_err:
	g_strfreev(argv);
	return NULL;
}

static int msg_startw(int timeout, const char *fifo_path, enum msg_type type)
{
        struct stat sbuf;
        int fifo;

        if (stat(fifo_path, &sbuf))
                return -1;
	if (!S_ISFIFO(sbuf.st_mode)) {
		g_warning("%s is not a FIFO", fifo_path);
		return -1;
	}

        fifo = open(fifo_path, O_WRONLY | O_NONBLOCK);
        if (fifo == -1)
                return -1;
	lockf(fifo, F_LOCK, 0);
	if (!write_full(fifo, &type, sizeof(type), 0)) {
		lockf(fifo, F_ULOCK, 0);
		close(fifo);
		return -1;
	}
	return fifo;
}
static void msg_endw(int fifo)
{
	if (fifo != -1) {
		lockf(fifo, F_ULOCK, 0);
		close(fifo);
	}
}
static enum msg_type msg_startr(int fifo)
{
	enum msg_type buf;
	if (read_full(fifo, &buf, sizeof(buf), 0))
		return buf;
	return MSG_INVAL;
}

//Attempt to connect to existing instance and launch a shell there
bool client_start(int timeout, const char *fifo_path, int argc, char **argv)
{
	bool ret = true;
        int fifo;
	if ((fifo = msg_startw(timeout, fifo_path, MSG_ARGV)) == -1) {
		ret = false;
		goto done;
	}
        if (!argv_write(fifo, argc, argv))
		ret = false;
done:
	msg_endw(fifo);
        return ret;
}
//Tell server we're replacing it
bool server_replace_notify(int timeout, const char *fifo_path)
{
	bool ret;
	int fifo;
	ret = ((fifo = msg_startw(timeout, fifo_path, MSG_REPLACE)) == -1);
	msg_endw(fifo);
	return ret;
}
//Event for new FIFO data
gboolean on_fifo_data(GIOChannel *source, GIOCondition condition, gpointer data)
{
	int fifo_fd;
        char **argv = NULL;

	fifo_fd = g_io_channel_unix_get_fd(source);

	switch (msg_startr(fifo_fd)) {
		case MSG_REPLACE:
			fifo_replaced = true;
			break;
		case MSG_ARGV:
			argv = argv_read(fifo_fd);
        		if (!term_start(data, argv)) {
                		g_warning("could not start terminal from fifo");
				g_strfreev(argv);
			}
			break;
		default:
			g_warning("Invalid message received on FIFO");
			break;
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
	/* POSIX says this is bad. It probably is. Yet it is necessary to open
	 * rw to allow
	 *  1) Non-blocking behavior on open, and
	 *  2) data to be cleared from the buffer on read() on FreeBSD.
	 *
	 * The first makes sense, fuck if I can understand why the second is
	 * happening. FIXME eventually, maybe... */
	if ((fifo_fd = open(fifo_path, O_RDWR)) == -1) {
		unlink(fifo_path);
		return false;
	}
	fifo = g_io_channel_unix_new(fifo_fd);
	g_io_add_watch(fifo, G_IO_IN, on_fifo_data, terms);

	return true;
}
