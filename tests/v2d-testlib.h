#ifndef _V2D_TESTLIB_H
#define _V2D_TESTLIB_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "v2d_ioctl.h"

extern int v2d_prepare(char *path, int width, int height);
extern int v2d_write_single(int fd, uint32_t cmd);
extern char *v2d_mmap(int fd, int width, int height);
void v2d_print_canvas(char *canvas, int width, int height);

#define v2d_write_single_checked(fd, cmd, msg) if (v2d_write_single(fd, cmd) != 4) { \
    perror(msg); \
    exit(1); \
}

#define v2d_write_single_invalid(fd, cmd, msg) if (v2d_write_single(fd, cmd) != -1 || errno != EINVAL) { \
    fprintf(stderr, "%d: cmd %08x should fail with EINVAL bacause of: %s\n", __LINE__, cmd, msg); \
    exit(1); \
}

#endif
