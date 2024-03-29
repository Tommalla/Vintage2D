#ifndef VINTAGE2D_H
#define VINTAGE2D_H

/* PCI ids */

#define VINTAGE2D_VENDOR_ID     0x1af4
#define VINTAGE2D_DEVICE_ID     0x10f2

/* Registers */

#define VINTAGE2D_ENABLE            0x000
#define VINTAGE2D_ENABLE_DRAW           0x00000001
#define VINTAGE2D_ENABLE_FETCH_CMD      0x00000004
#define VINTAGE2D_STATUS            0x004
#define VINTAGE2D_STATUS_DRAW           0x00000001
#define VINTAGE2D_STATUS_FIFO           0x00000002
#define VINTAGE2D_STATUS_FETCH_CMD      0x00000004
#define VINTAGE2D_INTR              0x008
#define VINTAGE2D_INTR_NOTIFY           0x00000001
#define VINTAGE2D_INTR_INVALID_CMD      0x00000002
#define VINTAGE2D_INTR_PAGE_FAULT       0x00000004
#define VINTAGE2D_INTR_CANVAS_OVERFLOW      0x00000008
#define VINTAGE2D_INTR_FIFO_OVERFLOW        0x00000010
#define VINTAGE2D_INTR_ENABLE           0x00c
#define VINTAGE2D_RESET             0x010
#define VINTAGE2D_RESET_DRAW            0x00000001
#define VINTAGE2D_RESET_FIFO            0x00000002
#define VINTAGE2D_RESET_TLB         0x00000008
#define VINTAGE2D_COUNTER           0x014
#define VINTAGE2D_FIFO_SEND         0x018
#define VINTAGE2D_FIFO_FREE         0x01c
#define VINTAGE2D_CMD_READ_PTR          0x020
#define VINTAGE2D_CMD_WRITE_PTR         0x024
#define VINTAGE2D_DRAW_STATE_CANVAS_PT      0x040
#define VINTAGE2D_DRAW_STATE_CANVAS_DIMS    0x044
#define VINTAGE2D_DRAW_STATE_SRC_POS        0x048
#define VINTAGE2D_DRAW_STATE_DST_POS        0x04c
#define VINTAGE2D_DRAW_STATE_FILL_COLOR     0x050
#define VINTAGE2D_DO_BLIT           0x054
#define VINTAGE2D_DO_FILL           0x058
#define VINTAGE2D_DRAW_STATE_COUNTER        0x05c
#define VINTAGE2D_SRC_TLB_TAG           0x060
#define VINTAGE2D_SRC_TLB_TAG_PT(tag)       ((tag) & 0xfffff000)
#define VINTAGE2D_SRC_TLB_TAG_IDX(tag)      ((tag) >> 2 & 0x3ff)
#define VINTAGE2D_SRC_TLB_PTE           0x064
#define VINTAGE2D_DST_TLB_TAG           0x068
#define VINTAGE2D_DST_TLB_TAG_PT(tag)       ((tag) & 0xfffff000)
#define VINTAGE2D_DST_TLB_TAG_IDX(tag)      ((tag) >> 2 & 0x3ff)
#define VINTAGE2D_DST_TLB_PTE           0x06c
#define VINTAGE2D_DRAW_STATE            0x070
#define VINTAGE2D_DRAW_STATE_MODE_BLIT      0x000000001
#define VINTAGE2D_DRAW_STATE_DIR_X_LEFT     0x000000004
#define VINTAGE2D_DRAW_STATE_DIR_Y_UP       0x000000008
#define VINTAGE2D_DRAW_STATE_WIDTHM1(st)    ((st) >> 4 & 0x7ff)
#define VINTAGE2D_DRAW_LEFT         0x074
#define VINTAGE2D_DRAW_LEFT_PIXELS(st)      ((st) & 0xfff)
#define VINTAGE2D_DRAW_LEFT_LINES(st)       ((st) >> 12 & 0xfff)
#define VINTAGE2D_FIFO_STATE            0x078
#define VINTAGE2D_FIFO_STATE_READ(st)       ((st) & 0x3f)
#define VINTAGE2D_FIFO_STATE_WRITE(st)      ((st) >> 8 & 0x3f)
#define VINTAGE2D_FIFO_CMD(x)           (0x080 + (x) * 4)
#define VINTAGE2D_FIFO_CMD_NUM          0x20

/* Commands */

#define VINTAGE2D_CMD_TYPE(cmd)         ((cmd) & 0xfc)
#define VINTAGE2D_CMD_TYPE_CANVAS_PT        0x00
#define VINTAGE2D_CMD_TYPE_CANVAS_DIMS      0x04
#define VINTAGE2D_CMD_TYPE_SRC_POS      0x08
#define VINTAGE2D_CMD_TYPE_DST_POS      0x0c
#define VINTAGE2D_CMD_TYPE_FILL_COLOR       0x10
#define VINTAGE2D_CMD_TYPE_DO_BLIT      0x14
#define VINTAGE2D_CMD_TYPE_DO_FILL      0x18
#define VINTAGE2D_CMD_TYPE_COUNTER      0x1c
#define VINTAGE2D_CMD_KIND(cmd)         ((cmd) & 3)
#define VINTAGE2D_CMD_KIND_CMD          0
#define VINTAGE2D_CMD_KIND_CMD_NOTIFY       1
#define VINTAGE2D_CMD_KIND_JUMP         2

#define VINTAGE2D_CMD_CANVAS_PT(addr, notify)   (VINTAGE2D_CMD_TYPE_CANVAS_PT | addr | notify)
#define VINTAGE2D_CMD_CANVAS_DIMS(w, h, notify) (VINTAGE2D_CMD_TYPE_CANVAS_DIMS | ((w) - 1) << 8 | ((h) - 1) << 20 | (notify))
#define VINTAGE2D_CMD_SRC_POS(x, y, notify) (VINTAGE2D_CMD_TYPE_SRC_POS | (x) << 8 | (y) << 20 | (notify))
#define VINTAGE2D_CMD_DST_POS(x, y, notify) (VINTAGE2D_CMD_TYPE_DST_POS | (x) << 8 | (y) << 20 | (notify))
#define VINTAGE2D_CMD_FILL_COLOR(c, notify) (VINTAGE2D_CMD_TYPE_FILL_COLOR | (c) << 8 | (notify))
#define VINTAGE2D_CMD_DO_BLIT(w, h, notify) (VINTAGE2D_CMD_TYPE_DO_BLIT | ((w) - 1) << 8 | ((h) - 1) << 20 | (notify))
#define VINTAGE2D_CMD_DO_FILL(w, h, notify) (VINTAGE2D_CMD_TYPE_DO_FILL | ((w) - 1) << 8 | ((h) - 1) << 20 | (notify))
#define VINTAGE2D_CMD_COUNTER(ctr, notify)  (VINTAGE2D_CMD_TYPE_COUNTER | (ctr) << 8 | (notify))

#define VINTAGE2D_CMD_PT(cmd)           ((cmd) & 0xfffff000)
#define VINTAGE2D_CMD_POS_X(cmd)        ((cmd) >> 8 & 0x7ff)
#define VINTAGE2D_CMD_POS_Y(cmd)        ((cmd) >> 20 & 0x7ff)
#define VINTAGE2D_CMD_WIDTH(cmd)        (((cmd) >> 8 & 0x7ff) + 1)
#define VINTAGE2D_CMD_HEIGHT(cmd)       (((cmd) >> 20 & 0x7ff) + 1)
#define VINTAGE2D_CMD_COLOR(cmd)        ((cmd) >> 8 & 0xff)
#define VINTAGE2D_CMD_COUNTER_VALUE(cmd)    ((cmd) >> 8 & 0xffffff)

/* Page tables */

#define VINTAGE2D_PTE_VALID     0x00000001
#define VINTAGE2D_PAGE_SHIFT        12
#define VINTAGE2D_PAGE_SIZE     0x1000

#endif