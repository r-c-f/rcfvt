
#ifndef FDIO_FULL_H_INC
#define FDIO_FULL_H_INC

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

/* functions to do read/write without shortness */
static bool read_full(int fd, void *buf, size_t count)
{
	ssize_t ret;
	char *pos;

	for (pos = buf; count;) {
		errno = 0;
		ret = read(fd, pos, count > SSIZE_MAX ? SSIZE_MAX : count);
		if (ret < 1) {
			switch (errno) {
				case EAGAIN:
				#if EAGAIN != EWOULDBLOCK
				case EWOULDBLOCK:
				#endif
				case EINTR:
					continue;
				default:
					return false;
			}
		}
		pos += ret;
		count -= ret;
	}
	return true;
}
static bool write_full(int fd, const void *buf, size_t count)
{
	ssize_t ret;
	const char *pos;

	for (pos = buf; count;) {
		errno = 0;
		ret = write(fd, pos, count > SSIZE_MAX ? SSIZE_MAX : count);
		if (ret < 1) {
			switch (errno) {
				case EAGAIN:
				#if EAGAIN != EWOULDBLOCK
				case EWOULDBLOCK:
				#endif
				case EINTR:
					continue;
				default:
					return false;
			}
		}
		pos += ret;
		count -= ret;
	}
	return true;
}

#endif
