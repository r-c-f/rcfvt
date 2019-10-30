#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <glib.h>


extern bool argv_write(int fd, int argc, char **argv);
extern char **argv_read(int fd);
extern bool client_start(int timeout, const char *fifo_path, int argc, char **argv);
extern bool server_start(char *fifo_path, GSList **terms);