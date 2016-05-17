#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "v2d_ioctl.h"

int v2d_prepare(char *path, int width, int height) {
    struct v2d_ioctl_set_dimensions dimm = {
        .height = height,
        .width = width,
    };
    int fd;
    int ret;

    if (!path)
        path = "/dev/v2d0";

    fd = open(path, O_RDWR);

    if (fd == -1)
        return fd;
    ret = ioctl(fd, V2D_IOCTL_SET_DIMENSIONS, &dimm);
    if (ret == -1)
        return ret;
    return fd;
}

int v2d_write_single(int fd, uint32_t cmd) {
    return write(fd, &cmd, 4);
}

char *v2d_mmap(int fd, int width, int height) {
    return mmap(NULL, width*height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

void v2d_print_canvas(char *canvas, int width, int height) {
    int i, j;
    for (i = 0; i < width; i++) {
        for (j = 0; j < height; j++)
            printf("%02hhX ", canvas[i*width+j]);
        printf("\n");
    }
}
