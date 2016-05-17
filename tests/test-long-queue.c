#include <sys/mman.h>
#include <string.h>
#include "v2d-testlib.h"


int main(int argc, char **argv) {
    int fd;
    char *canvas;
    int i;

    fd = v2d_prepare(NULL, 2048, 2048);
    if (fd < 0) {
        perror("v2d_prepare");
        exit(1);
    }
    canvas = v2d_mmap(fd, 2048, 2048);
    if (canvas == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(canvas, 0, 2048*2048);
    for (i = 0; i < 2048; i++) {
        v2d_write_single_checked(fd, V2D_CMD_DST_POS(i, i), "V2D_CMD_DST_POS");
        v2d_write_single_checked(fd, V2D_CMD_FILL_COLOR(0xff), "V2D_CMD_FILL_COLOR");
        v2d_write_single_checked(fd, V2D_CMD_DO_FILL(1, 1), "V2D_CMD_DO_FILL");
        v2d_write_single_checked(fd, V2D_CMD_SRC_POS(i, i), "V2D_CMD_SRC_POS");
        v2d_write_single_checked(fd, V2D_CMD_DST_POS(2047-i, i), "V2D_CMD_DST_POS");
        v2d_write_single_checked(fd, V2D_CMD_DO_BLIT(1, 1), "V2D_CMD_DO_BLIT");
    }
    fsync(fd);
    v2d_print_canvas(canvas, 2048, 2048);

    return 0;
}
