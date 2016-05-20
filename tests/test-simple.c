#include <sys/mman.h>
#include <string.h>
#include "v2d-testlib.h"


int main(int argc, char **argv) {
    int fd;
    char *canvas;

    fd = v2d_prepare(NULL, 10, 10);
    if (fd < 0) {
        perror("v2d_prepare");
        exit(1);
    }
    printf("Initialized...\n");
    canvas = v2d_mmap(fd, 10, 10);
    printf("mmap finished...\n");
    if (canvas == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(canvas, 0, 10*10);
    printf("Writes...\n");
    v2d_write_single_checked(fd, V2D_CMD_DST_POS(0, 0), "V2D_CMD_DST_POS");
    v2d_write_single_checked(fd, V2D_CMD_FILL_COLOR(0xff), "V2D_CMD_FILL_COLOR");
    v2d_write_single_checked(fd, V2D_CMD_DO_FILL(5, 5), "V2D_CMD_DO_FILL");
    v2d_write_single_checked(fd, V2D_CMD_SRC_POS(0, 0), "V2D_CMD_SRC_POS");
    v2d_write_single_checked(fd, V2D_CMD_DST_POS(5, 5), "V2D_CMD_DST_POS");
    v2d_write_single_checked(fd, V2D_CMD_DO_BLIT(5, 5), "V2D_CMD_DO_BLIT");
    fsync(fd);
    v2d_print_canvas(canvas, 10, 10);

    return 0;
}
