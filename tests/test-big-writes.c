#include <sys/mman.h>
#include <string.h>
#include "v2d-testlib.h"


#define BR_SIZE 1022

int main(int argc, char **argv) {
    int fd;
    char *canvas;
    int i, rc;
    uint32_t *cmds;
    pid_t pid;
    int color, size, bg;

    switch (pid = fork()) {
        case -1:
            perror("fork");
            exit(1);
        case 0:
            switch (pid = fork()) {
                case -1:
                    perror("fork");
                    exit(1);
                case 0:
                    switch (pid = fork()) {
                        case -1:
                            perror("fork");
                            exit(1);
                        case 0:
                            color = 0x27;
                            size = BR_SIZE;
                            bg = 0x33;
                            break;
                        default:
                            color = 0x3c;
                            size = BR_SIZE;
                            bg = 0xdd;
                            break;
                    }
                    break;
                default:
                    color = 0x4a;
                    size = BR_SIZE;
                    bg = 0xee;
                    break;
            }
            break;
        default:
            color = 0x17;
            size = BR_SIZE;
            bg = 0xcc;
            break;
    }
    fd = v2d_prepare(NULL, size, size);
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
    cmds = malloc(6*size*sizeof(uint32_t));
    if (!cmds) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (i = 0; i < size; i++) {
        cmds[i*6+0] = V2D_CMD_DST_POS(i, i);
        cmds[i*6+1] = V2D_CMD_FILL_COLOR(color);
        cmds[i*6+2] = V2D_CMD_DO_FILL(1, 1);
        cmds[i*6+3] = V2D_CMD_SRC_POS(i, i);
        cmds[i*6+4] = V2D_CMD_DST_POS(size-1-i, i);
        cmds[i*6+5] = V2D_CMD_DO_BLIT(1, 1);
    }
    i = 0;
    while (i < 6*size) {
        rc = write(fd, cmds+i, (6*size-i)*sizeof(*cmds));
        if (rc == -1) {
            perror("write");
            exit(1);
        }
        if (rc & 3) {
            fprintf(stderr, "write returned %d ?!\n", rc);
            exit(1);
        }
        i += rc >> 2;
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
