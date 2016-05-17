#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "v2d-testlib.h"

static int set_size(int fd, int width, int height) {
    struct v2d_ioctl_set_dimensions dimm = {
        .height = height,
        .width = width,
    };
    return ioctl(fd, V2D_IOCTL_SET_DIMENSIONS, &dimm);
}

int main(int argc, char **argv) {
    int fd, i;
    uint32_t cmd;
    char *canvas;

    fd = open("/dev/v2d0", O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    if (set_size(fd, 3000, 3000) != -1 || errno != EINVAL) {
        fprintf(stderr, "V2D_IOCTL_SET_DIMENSIONS should fail - too large canvas\n");
        exit(1);
    }
    if (ioctl(fd, 1, 0) != -1 || errno != ENOTTY) {
        fprintf(stderr, "invalid ioctl should be rejected with ENOTTY\n");
        exit(1);
    }
    v2d_write_single_invalid(fd, V2D_CMD_DST_POS(50, 50), "before set_size");
    if (set_size(fd, 100, 100) != 0) {
        perror("set_size");
        exit(1);
    }
    if (set_size(fd, 10, 10) != -1 || errno != EINVAL) {
        fprintf(stderr, "second V2D_IOCTL_SET_DIMENSIONS should fail\n");
        exit(1);
    }
    canvas = v2d_mmap(fd, 100, 100);
    if (canvas == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(canvas, 0, 100*100);
    cmd = V2D_CMD_DST_POS(50, 50);
    if (write(fd, &cmd, 3) != -1) {
        fprintf(stderr, "not aligned write should fail\n");
        exit(1);
    }
    for (cmd = 0; cmd < 0x100; cmd++) {
        if (cmd == V2D_CMD_TYPE_SRC_POS)
            continue;
        if (cmd == V2D_CMD_TYPE_DST_POS)
            continue;
        if (cmd == V2D_CMD_TYPE_FILL_COLOR)
            continue;
        if (cmd == V2D_CMD_TYPE_DO_BLIT)
            continue;
        if (cmd == V2D_CMD_TYPE_DO_FILL)
            continue;
        v2d_write_single_invalid(fd, cmd, "invalid cmd");
    }
    v2d_write_single_invalid(fd, V2D_CMD_SRC_POS(50, 50) | 1<<19, "unexpected bits set");
    v2d_write_single_invalid(fd, V2D_CMD_SRC_POS(50, 50) | 1<<31, "unexpected bits set");
    v2d_write_single_invalid(fd, V2D_CMD_DST_POS(50, 50) | 1<<19, "unexpected bits set");
    v2d_write_single_invalid(fd, V2D_CMD_DST_POS(50, 50) | 1<<31, "unexpected bits set");
    for (i = 16; i < 32; i++)
        v2d_write_single_invalid(fd, V2D_CMD_FILL_COLOR(0xff) | 1<<i, "unexpected bits set");
    v2d_write_single_invalid(fd, V2D_CMD_DST_POS(500, 50), "outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_DST_POS(50, 500), "outside of canvas");
    /* check if the second set_size really was ignored */
    v2d_write_single_checked(fd, V2D_CMD_DST_POS(50, 50), "V2D_CMD_DST_POS");
    v2d_write_single_invalid(fd, V2D_CMD_DO_FILL(5, 5), "V2D_CMD_DO_FILL without V2D_CMD_FILL_COLOR");
    v2d_write_single_checked(fd, V2D_CMD_FILL_COLOR(0xff), "V2D_CMD_FILL_COLOR");
    v2d_write_single_invalid(fd, V2D_CMD_DO_FILL(300, 300), "FILL: outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_DO_FILL(51, 50), "FILL: outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_DO_FILL(50, 51), "FILL: outside of canvas");
    v2d_write_single_checked(fd, V2D_CMD_DO_FILL(50, 50), "V2D_CMD_DO_FILL");
    v2d_write_single_invalid(fd, V2D_CMD_DO_FILL(5, 5), "V2D_CMD_DO_FILL without V2D_CMD_DST_POS");

    v2d_write_single_invalid(fd, V2D_CMD_SRC_POS(200, 10), "V2D_CMD_SRC_POS outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_SRC_POS(20, 1000), "V2D_CMD_SRC_POS outside of canvas");

    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(50, 50), "V2D_CMD_DO_BLIT without V2D_CMD_SRC_POS");
    v2d_write_single_checked(fd, V2D_CMD_SRC_POS(50, 50), "V2D_CMD_SRC_POS");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(50, 50), "V2D_CMD_DO_BLIT without V2D_CMD_DST_POS");
    v2d_write_single_checked(fd, V2D_CMD_DST_POS(5, 5), "V2D_CMD_DST_POS");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(51, 50), "BLIT: src outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(50, 51), "BLIT: src outside of canvas");
    v2d_write_single_checked(fd, V2D_CMD_DO_BLIT(50, 50), "V2D_CMD_DO_BLIT");
    v2d_write_single_checked(fd, V2D_CMD_DST_POS(5, 5), "V2D_CMD_DST_POS");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(5, 5), "V2D_CMD_DO_BLIT without V2D_CMD_SRC_POS");
    v2d_write_single_checked(fd, V2D_CMD_SRC_POS(1, 1), "V2D_CMD_SRC_POS");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(96, 95), "BLIT: dst outside of canvas");
    v2d_write_single_invalid(fd, V2D_CMD_DO_BLIT(95, 96), "BLIT: dst outside of canvas");
    fsync(fd);
    v2d_print_canvas(canvas, 100, 100);

    return 0;
}
