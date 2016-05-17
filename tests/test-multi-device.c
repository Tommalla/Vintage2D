#include <sys/mman.h>
#include <string.h>
#include "v2d-testlib.h"


int main(int argc, char **argv) {
    int fd;
    char *canvas;
    int i;
    pid_t pid;
    int color, size, bg;

    switch (pid = fork()) {
        case -1:
            perror("fork");
            exit(1);
        case 0:
            color = 0x3f;
            size = 2048;
            bg = 0x11;
            fd = v2d_prepare("/dev/v2d0", size, size);
            break;
        default:
            color = 0x17;
            size = 1024;
            bg = 0xcc;
            fd = v2d_prepare("/dev/v2d1", size, size);
            break;
    }
    if (fd < 0) {
        perror("v2d_prepare");
        exit(1);
    }
    canvas = v2d_mmap(fd, size, size);
    if (canvas == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(canvas, bg, size*size);
    for (i = 0; i < size; i++) {
        v2d_write_single_checked(fd, V2D_CMD_DST_POS(i, i), "V2D_CMD_DST_POS");
        v2d_write_single_checked(fd, V2D_CMD_FILL_COLOR(color), "V2D_CMD_FILL_COLOR");
        v2d_write_single_checked(fd, V2D_CMD_DO_FILL(1, 1), "V2D_CMD_DO_FILL");
        v2d_write_single_checked(fd, V2D_CMD_SRC_POS(i, i), "V2D_CMD_SRC_POS");
        v2d_write_single_checked(fd, V2D_CMD_DST_POS(size-1-i, i), "V2D_CMD_DST_POS");
        v2d_write_single_checked(fd, V2D_CMD_DO_BLIT(1, 1), "V2D_CMD_DO_BLIT");
    }
    fsync(fd);
    if (pid) {
        int status;
        waitpid(pid, &status, 0);
        if (WEXITSTATUS(status))
            exit(WEXITSTATUS(status));
    }
    v2d_print_canvas(canvas, size, size);

    return 0;
}
