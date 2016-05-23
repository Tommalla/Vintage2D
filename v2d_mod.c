/* Tomasz Zakrzewski, tz336079      /
 * ZSO 2015/2016, Vintage2D driver */
// Kernel includes
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/circ_buf.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include "vintage2d.h"
#include "v2d_ioctl.h"

#define V2D_UNINITIALIZED_ERR "Trying to use driver before initializing context with canvas size."
#define V2D_MAX_COUNT 256
#define V2D_MAX_DIMM 2048
#define V2D_COORD_UNSET (1 << 31)
#define V2D_COLOR_UNSET 257
#define V2D_CMD_QUEUE_SIZE 256
#define V2D_COUNTER_MOD (1 << 24)

static struct class *v2d_class = NULL;
dev_t dev_base = 0;
static DEFINE_IDR(v2ddev_idr);

struct v2d_pos {
    uint32_t x, y;
};

struct v2d_cmd_meta {
    struct v2d_user *u;
    uint32_t cmd;
};

struct v2d_data {
    struct pci_dev *pdev;
    struct cdev cdev;
    void __iomem *bar0;
    struct dma_pool *canvas_pool;
    struct mutex dev_lock;
    struct v2d_user *last_user;
    uint64_t counter, last_counter_sync;
    uint32_t last_read_counter;

    // Queue
    dma_addr_t dev_cmd_queue;
    uint32_t *virt_cmd_queue;
    struct v2d_cmd_meta meta_queue[V2D_CMD_QUEUE_SIZE]; // Last pos unused.
    uint32_t head, tail, space;
    struct mutex queue_lock;
    wait_queue_head_t write_queue;
};

struct v2d_user {
    struct v2d_data *v2ddev;
    // Context data:
    int initialized;
    struct v2d_ioctl_set_dimensions dimm;
    struct mutex write_lock;
    void **vpages; // Canvas memory, page-by-page
    dma_addr_t *dpages;
    uint32_t *vpt;
    dma_addr_t dpt;
    int pages_num;
    struct v2d_pos src_pos, dst_pos;
    uint16_t color;
    uint64_t wait_for_counter;
    wait_queue_head_t fsync_queue;
};

static void set_read_ptr(struct v2d_data *v2ddev, dma_addr_t ptr) {
    iowrite32(ptr, v2ddev->bar0 + VINTAGE2D_CMD_READ_PTR);
}

static void set_write_ptr(struct v2d_data *v2ddev, dma_addr_t ptr) {
    iowrite32(ptr, v2ddev->bar0 + VINTAGE2D_CMD_WRITE_PTR);
}

static void reset_draw_params(struct v2d_user *u) {
    u->src_pos.x = u->src_pos.y = u->dst_pos.x = u->dst_pos.y = V2D_COORD_UNSET;
    u->color = V2D_COLOR_UNSET;
}

static void print_dev_status(struct v2d_data *v2ddev) {
    printk(KERN_NOTICE "READ = %08X, WRITE = %08X, INTR = %u, STATUS = %u, ENABLE = %u,  FIFO_FREE = %u\n",
           ioread32(v2ddev->bar0 + VINTAGE2D_CMD_READ_PTR), ioread32(v2ddev->bar0 + VINTAGE2D_CMD_WRITE_PTR),
           ioread32(v2ddev->bar0 + VINTAGE2D_INTR), ioread32(v2ddev->bar0 + VINTAGE2D_STATUS),
           ioread32(v2ddev->bar0 + VINTAGE2D_ENABLE),
           ioread32(v2ddev->bar0 + VINTAGE2D_FIFO_FREE));
    iowrite32(31, v2ddev->bar0 + VINTAGE2D_INTR);
}

static void reset(struct v2d_data *v2ddev, uint8_t reset_draw, uint8_t reset_fifo, uint8_t reset_tlb) {
    iowrite32((reset_draw ? VINTAGE2D_RESET_DRAW : 0) | (reset_fifo ? VINTAGE2D_RESET_FIFO : 0) |
              (reset_tlb ? VINTAGE2D_RESET_TLB : 0), v2ddev->bar0 + VINTAGE2D_RESET);
}

/* Requires queue lock */
static void update_space(struct v2d_data *v2ddev) {
    if (v2ddev->tail >= v2ddev->head) {
        v2ddev->space = V2D_CMD_QUEUE_SIZE - 1 - v2ddev->tail + v2ddev->head - 1;
    } else {
        v2ddev->space = v2ddev->head - v2ddev->tail - 1;
    }
}

/* Requires queue lock */
static void incr_head(struct v2d_data *v2ddev) {
    v2ddev->head++;
    if (v2ddev->head >= V2D_CMD_QUEUE_SIZE - 1) {
        v2ddev->head = 0;
    }
    update_space(v2ddev);
    printk(KERN_NOTICE "v2d: Moved head to %u, space = %u\n", v2ddev->head, v2ddev->space);
}

/* Requires queue lock */
static void incr_tail(struct v2d_data *v2ddev) {
    printk(KERN_NOTICE "incr_tail: %u\n", v2ddev->tail);
    v2ddev->tail++;
    if (v2ddev->tail >= V2D_CMD_QUEUE_SIZE - 1) {  // Intentionally skipping the last index (JUMP)
        v2ddev->tail = 0;
    }
    update_space(v2ddev);
    printk(KERN_NOTICE "v2d: Moved tail to %u, space = %u\n", v2ddev->tail, v2ddev->space);
}

static irqreturn_t v2d_irq(int irq, void *dev) {
    struct v2d_data *v2ddev;
    uint32_t intr, counter, delta;

    v2ddev = dev;

    intr = ioread32(v2ddev->bar0 + VINTAGE2D_INTR);
    iowrite32(intr, v2ddev->bar0 + VINTAGE2D_INTR);
    if (!intr) {
        return IRQ_NONE;
    }
    printk(KERN_NOTICE "IRQ\n");
    print_dev_status(v2ddev);
    printk(KERN_NOTICE "INTR = %d\n", intr);

    if (intr & VINTAGE2D_INTR_NOTIFY) {
        counter = ioread32(v2ddev->bar0 + VINTAGE2D_COUNTER);
        delta = counter - v2ddev->last_read_counter;
        v2ddev->last_counter_sync += delta;

        if (counter > v2ddev->counter) {
            return IRQ_HANDLED; // 'GARBAGE' IRQ
        }

        printk(KERN_NOTICE "Read counter=%u, last_read_counter=%u\n", counter, v2ddev->last_read_counter);
        // FIXME potential problem with garbage counter at the beginning?
        while (v2ddev->last_read_counter != counter) {
            if (VINTAGE2D_CMD_TYPE(v2ddev->meta_queue[v2ddev->head].cmd) == VINTAGE2D_CMD_TYPE_COUNTER) {
                struct v2d_user *u;
                u = v2ddev->meta_queue[v2ddev->head].u;
                v2ddev->last_read_counter = VINTAGE2D_CMD_COUNTER_VALUE(v2ddev->meta_queue[v2ddev->head].cmd);
                printk(KERN_NOTICE "counter=%d %d %llu\n",
                       VINTAGE2D_CMD_COUNTER_VALUE(v2ddev->meta_queue[v2ddev->head].cmd), counter, u->wait_for_counter);
                wake_up(&u->fsync_queue);
            }

            printk(KERN_NOTICE "Finished cmd: id = %u type = %u\n", v2ddev->head, VINTAGE2D_CMD_TYPE(v2ddev->meta_queue[v2ddev->head].cmd));
            incr_head(v2ddev);
            wake_up(&v2ddev->write_queue);
        }

    } else {
        printk(KERN_ERR "v2d irq: INTR = %d\n", intr);
    }

    printk(KERN_NOTICE "Handled.\n");

    return IRQ_HANDLED;
}

static int v2d_open(struct inode *i, struct file *f) {
    struct v2d_data *v2ddev;
    struct v2d_user *u;

    printk(KERN_NOTICE "v2dopen...\n");
    v2ddev = container_of(i->i_cdev, struct v2d_data, cdev);
    u = kmalloc(sizeof(struct v2d_user), GFP_KERNEL);
    if (!u) {
        return -ENOMEM;
    }
    u->v2ddev = v2ddev;
    u->initialized = 0;
    u->pages_num = 0;
    reset_draw_params(u);
    u->wait_for_counter = 0;
    f->private_data = u;
    mutex_init(&u->write_lock);
    init_waitqueue_head(&u->fsync_queue);
    return 0;
}

static int v2d_release(struct inode *i, struct file *f) {
    int j;
    struct v2d_user *u;

    printk(KERN_NOTICE "v2drelease...\n");
    u = f->private_data;

    if (u->v2ddev->last_user == u) {
        u->v2ddev->last_user = NULL;
    }

    if (u->initialized) {
        for (j = 0; j < u->pages_num; ++j) {
            dma_pool_free(u->v2ddev->canvas_pool, u->vpages[j], u->dpages[j]);
        }
        dma_pool_free(u->v2ddev->canvas_pool, u->vpt, u->dpt);
        kfree(u->vpages);
        kfree(u->dpages);
    }
    kfree(u);
    return 0;
}

/*
 * This function simply puts the command in all the releveant queues.
 * Write mutex must be held.
 */
static void send_command(struct v2d_user *u, uint32_t cmd) {
    struct v2d_data *v2ddev;

    v2ddev = u->v2ddev;
    // Put command in the queue.
    printk(KERN_NOTICE "Enqueueing: %u %u\n", v2ddev->tail, VINTAGE2D_CMD_TYPE(cmd));
    v2ddev->virt_cmd_queue[v2ddev->tail] = cmd;
    v2ddev->meta_queue[v2ddev->tail].u = u;
    v2ddev->meta_queue[v2ddev->tail].cmd = cmd;
    incr_tail(v2ddev);
    set_write_ptr(u->v2ddev, u->v2ddev->dev_cmd_queue + v2ddev->tail * sizeof(uint32_t));

    print_dev_status(u->v2ddev);
}

/* Write mutex must be held */
static void change_context(struct v2d_user *u) {
    printk(KERN_NOTICE "Canvas change, v2d: PT = %08X %u x %u\n", u->dpt, u->dimm.width, u->dimm.height);
    send_command(u, VINTAGE2D_CMD_CANVAS_PT((uint32_t)u->dpt, 0));
    send_command(u, VINTAGE2D_CMD_CANVAS_DIMS(u->dimm.width, u->dimm.height, 0));
    u->v2ddev->last_user = u;
}

/* Write mutex must be held */
static int enqueue(struct v2d_user *u, uint32_t cmd) {
    /* TODO enqueue in a queue */
    uint32_t size;
    uint8_t context_change;

    size = 2;  // +COUNTER

    if (VINTAGE2D_CMD_TYPE(cmd) == VINTAGE2D_CMD_TYPE_DO_FILL || VINTAGE2D_CMD_TYPE(cmd) == VINTAGE2D_CMD_TYPE_DO_BLIT) {
        size += 2; //DST_POS, SRC_POS / FILL_COLOR
    }

    mutex_lock(&u->v2ddev->queue_lock);

    while (u->v2ddev->space < size + (u->v2ddev->last_user != u ? 2 : 0)) {
        mutex_unlock(&u->v2ddev->queue_lock);
        wait_event(u->v2ddev->write_queue, u->v2ddev->space >= size + (u->v2ddev->last_user != u ? 2 : 0));
        mutex_lock(&u->v2ddev->queue_lock);
    }

    printk(KERN_NOTICE "Before sending commands: size = %u, space = %u\n", size, u->v2ddev->space);

    context_change = u->v2ddev->last_user != u ? 1 : 0; // Could have changed since we held mutex.

    if (context_change) {
        change_context(u);
    }

    switch (VINTAGE2D_CMD_TYPE(cmd)) {
        case VINTAGE2D_CMD_TYPE_DO_FILL:
            send_command(u, VINTAGE2D_CMD_DST_POS(u->dst_pos.x, u->dst_pos.y, 0));
            send_command(u, VINTAGE2D_CMD_FILL_COLOR(u->color, 0));
            send_command(u, VINTAGE2D_CMD_DO_FILL(VINTAGE2D_CMD_WIDTH(cmd), VINTAGE2D_CMD_HEIGHT(cmd), 0));
            break;
        case VINTAGE2D_CMD_TYPE_DO_BLIT:
            send_command(u, VINTAGE2D_CMD_DST_POS(u->dst_pos.x, u->dst_pos.y, 0));
            send_command(u, VINTAGE2D_CMD_SRC_POS(u->src_pos.x, u->src_pos.y, 0));
            send_command(u, VINTAGE2D_CMD_DO_BLIT(VINTAGE2D_CMD_WIDTH(cmd), VINTAGE2D_CMD_HEIGHT(cmd), 0));
            break;
    }

    printk(KERN_NOTICE "Sending counter = %llu\n", u->v2ddev->counter);
    send_command(u, VINTAGE2D_CMD_COUNTER(u->v2ddev->counter % V2D_COUNTER_MOD, 1));
    u->wait_for_counter = u->v2ddev->counter;
    u->v2ddev->counter++;

    mutex_unlock(&u->v2ddev->queue_lock);
    return 0;
}

static int is_rect_valid(struct v2d_user *u, const struct v2d_pos *pos, uint16_t width, uint16_t height) {
    return ((pos->x + width <= u->dimm.width) && (pos->y + height <= u->dimm.height));
}

/*
 * Puts the command (and the SRC_POS/DST_POS commands before it) in the command queue.
 * This method is safe - if there is no space on the queue, it will block until enqueueing
 * is possible.
 * Write lock must be held.
 */
static int enqueue_fill(struct v2d_user *u, uint32_t cmd) {
    if (u->dst_pos.x == V2D_COORD_UNSET || u->color == V2D_COLOR_UNSET) {
        printk(KERN_ERR "v2d: Tried to FILL without DST_POS or COLOR.\n");
        return -1;
    }

    if (!is_rect_valid(u, &u->dst_pos, V2D_CMD_WIDTH(cmd), V2D_CMD_HEIGHT(cmd))) {
        printk(KERN_ERR "v2d: Tried to do FILL outside canvas.\n");
        return -1;
    }

    enqueue(u, cmd);

    reset_draw_params(u);
    return 0;
}

static int enqueue_blit(struct v2d_user *u, uint32_t cmd) {
    uint16_t width, height;
    if (u->src_pos.x == V2D_COORD_UNSET || u->dst_pos.x == V2D_COORD_UNSET) {
        printk(KERN_ERR "v2d: Tried to BLIT without SRC_POS or DST_POS.\n");
        return -1;
    }

    width = V2D_CMD_WIDTH(cmd);
    height = V2D_CMD_HEIGHT(cmd);

    if (!is_rect_valid(u, &u->dst_pos, width, height) || !is_rect_valid(u, &u->src_pos, width, height)) {
        printk(KERN_ERR "v2d: Tried to do BLIT outside canvas.\n");
        return -1;
    }

    enqueue(u, cmd);

    reset_draw_params(u);
    return 0;
}

static int set_pos(struct v2d_user *u, uint32_t cmd, struct v2d_pos *dst) {
    uint16_t x, y;

    x = V2D_CMD_POS_X(cmd);
    y = V2D_CMD_POS_Y(cmd);

    if (V2D_CMD_SRC_POS(x, y) != cmd && V2D_CMD_DST_POS(x, y) != cmd) {
        printk(KERN_ERR "v2d: corrupted POS_{SRC,DST}\n");
        return -EINVAL;
    }

    if (x > u->dimm.width || y > u->dimm.height) {
        printk(KERN_ERR "v2d: tried to set pos outside canvas");
        return -EINVAL;
    }

    dst->x = x;
    dst->y = y;
    return 0;
}

static ssize_t v2d_write(struct file *f, const char __user *buf, size_t size, loff_t *off) {
    struct v2d_user *u;
    char *data, *ptr;
    int rc, real_commands;

    real_commands = 0;  // DO_*

    u = f->private_data;
    if (!u->initialized) {
        printk(KERN_ERR V2D_UNINITIALIZED_ERR);
        return -EINVAL;
    }

    if (size % 4) { // Not divisible by 32 bits
        printk(KERN_ERR "v2d: Wrong write size: %u\n", size);
        return -EINVAL;
    }

    if (!size) {
        return 0;
    }

    data = kmalloc(sizeof(char) * size, GFP_KERNEL);

    mutex_lock(&u->write_lock);
    if (copy_from_user(data, buf, size)) {
        mutex_unlock(&u->write_lock);
        return -EFAULT;
    }

    rc = size;

    print_dev_status(u->v2ddev);

    for (ptr = data; ptr < data + size; ptr += 4) {
        uint32_t cmd = *(uint32_t*)ptr;
        uint8_t cmd_type = cmd % (1 << 8);
        uint32_t c;
        switch (cmd_type) {
            case V2D_CMD_TYPE_SRC_POS:
                if (set_pos(u, cmd, &u->src_pos)) {
                    rc = -EINVAL;
                }
                break;

            case V2D_CMD_TYPE_DST_POS:
                if (set_pos(u, cmd, &u->dst_pos)) {
                    rc = -EINVAL;
                }
                break;

            case V2D_CMD_TYPE_FILL_COLOR:
                c = V2D_CMD_COLOR(cmd);
                if (cmd != V2D_CMD_FILL_COLOR(c)) {
                    printk(KERN_ERR "v2d: FILL_COLOR: 16-31th bits not 0.\n");
                    rc = -EINVAL;
                } else {
                    u->color = c;
                }
                break;

            case V2D_CMD_TYPE_DO_BLIT:
                if (enqueue_blit(u, cmd)) {
                    rc = -EINVAL;
                }
                real_commands++;
                break;
            case V2D_CMD_TYPE_DO_FILL:
                if (enqueue_fill(u, cmd)) {
                    rc = -EINVAL;
                }
                real_commands++;
                break;

            default:
                printk(KERN_ERR "v2d: Unknown command: %02X\n", cmd_type);
                rc = -EINVAL;
        }
    }

    mutex_unlock(&u->write_lock);
    kfree(data);

    return rc;
}

static long v2d_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    struct v2d_user *u;
    unsigned int size, i;
    u = f->private_data;

    if (cmd != V2D_IOCTL_SET_DIMENSIONS) {
        printk(KERN_ERR "ioctl used for an unknown command: %u\n", cmd);
        return -ENOTTY;
    }

    if (u->initialized) {
        printk(KERN_ERR "trying to set canvas dimensions for the second time\n");
        return -EINVAL;
    }

    if (copy_from_user(&(u->dimm), (struct v2d_ioctl_set_dimensions*)arg, sizeof(struct v2d_ioctl_set_dimensions))) {
                return -EFAULT;
    }

    size = u->dimm.width * u->dimm.height;

    if (size == 0 || u->dimm.width > V2D_MAX_DIMM || u->dimm.height > V2D_MAX_DIMM) {
        return -EINVAL;
    }

    u->pages_num = size / VINTAGE2D_PAGE_SIZE + ((size % VINTAGE2D_PAGE_SIZE) > 0 ? 1 : 0);

    // FIXME handle ENOMEM

    u->vpages = kmalloc(sizeof(void *) * u->pages_num, GFP_KERNEL);
    u->dpages = kmalloc(sizeof(dma_addr_t) * u->pages_num, GFP_KERNEL);
    u->vpt = dma_pool_alloc(u->v2ddev->canvas_pool, GFP_KERNEL, &u->dpt);   // There will never be more than 1024 pages,
                                                                            // so a page is enough and well-aligned.
    for (i = 0; i < u->pages_num; ++i) {
        u->vpages[i] = dma_pool_alloc(u->v2ddev->canvas_pool, GFP_KERNEL, &u->dpages[i]);
        u->vpt[i] = ((u->dpages[i] >> VINTAGE2D_PAGE_SHIFT) << VINTAGE2D_PAGE_SHIFT) | VINTAGE2D_PTE_VALID;
    }
    u->initialized = 1;

    printk(KERN_NOTICE "v2d: initialized canvas to %u x %u. Pages num = %u\n", u->dimm.width, u->dimm.height,
           u->pages_num);

    return 0;
}

static int v2d_mmap(struct file *f, struct vm_area_struct *vma) {
    struct v2d_user *u;
    unsigned long uaddr, usize;
    int i;

    uaddr = vma->vm_start;
    usize = vma->vm_end - vma->vm_start;
    // NOTICE: Assumption: we're working on x86, so PAGE_SIZE = VINTAGE2D_PAGE_SIZE. Phew.
    i = vma->vm_pgoff;

    u = f->private_data;
    if (!u->initialized) {
        printk(KERN_ERR V2D_UNINITIALIZED_ERR);
        return -EINVAL;
    }

    if (usize > u->pages_num * VINTAGE2D_PAGE_SIZE) {
        printk(KERN_ERR "v2d: Tried to map a canvas bigger than allocated.\n");
        return -EINVAL;
    }

    if (i >= u->pages_num) {
        printk(KERN_ERR "v2d: (Possible drvier bug) tried to map a page id bigger than pages_num: %d\n", i);
        return -EINVAL;
    }

    do {
        int rc;
        rc = vm_insert_page(vma, uaddr, virt_to_page(u->vpages[i++]));
        if (rc) {
            printk(KERN_ERR "v2d: Remapping memory error: %d\n", rc);
            return rc;
        }

        uaddr += VINTAGE2D_PAGE_SIZE;
        usize -= VINTAGE2D_PAGE_SIZE;
    } while (usize > 0);

    return 0;
}

static int v2d_fsync(struct file *f, loff_t a, loff_t b, int datasync) {
    struct v2d_user *u;
    uint64_t wait_for_counter_copy;

    u = f->private_data;
    if (!u->initialized) {
        printk(KERN_ERR V2D_UNINITIALIZED_ERR);
        return -EINVAL;
    }

    mutex_lock(&u->write_lock);
    wait_for_counter_copy = u->wait_for_counter;
    while (wait_for_counter_copy > u->v2ddev->last_counter_sync) {
        mutex_unlock(&u->write_lock);
        wait_event(u->fsync_queue, wait_for_counter_copy <= u->v2ddev->last_counter_sync);
        mutex_lock(&u->write_lock);
    }

    wake_up(&u->fsync_queue);
    mutex_unlock(&u->write_lock);
    return 0;
}

static struct file_operations v2d_fops = {
    .owner = THIS_MODULE,
    .open = v2d_open,
    .release = v2d_release,
    .write = v2d_write,
    .unlocked_ioctl = v2d_ioctl,
    .mmap = v2d_mmap,
    .fsync = v2d_fsync,
};

static int v2d_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    struct v2d_data *v2ddev;
    int minor = 0;
    int rc;
    struct device *device;

    printk(KERN_NOTICE "v2d_probe...\n");

    v2ddev = kmalloc(sizeof(struct v2d_data), GFP_KERNEL);
    if (!v2ddev) {
        return -ENOMEM;
    }
    v2ddev->pdev = dev;
    cdev_init(&v2ddev->cdev, &v2d_fops);

    rc = pci_enable_device(dev);
    if (IS_ERR_VALUE(rc)) {
        goto err_pci_enable;
    }
    rc = pci_request_regions(dev, "v2ddrv");
    if (IS_ERR_VALUE(rc)) {
        goto err_pci_region;
    }

    v2ddev->bar0 = pci_iomap(dev, 0, 0);
    if (!v2ddev->bar0) {
        rc = PTR_ERR(v2ddev->bar0);
        goto err_iomap;
    }

    pci_set_master(dev);
    rc = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
    if (IS_ERR_VALUE(rc)) {
        goto err_dma_mask;
    }

    rc = request_irq(dev->irq, v2d_irq, IRQF_SHARED, "v2ddev", v2ddev);
    if (IS_ERR_VALUE(rc)) {
        goto err_irq;
    }

    v2ddev->canvas_pool = dma_pool_create("v2ddev canvas", &v2ddev->pdev->dev,
            VINTAGE2D_PAGE_SIZE, VINTAGE2D_PAGE_SIZE, 0);
    if (!v2ddev->canvas_pool) {
        rc = -ENOMEM;
        goto err_canvas_pool;
    }

    v2ddev->virt_cmd_queue = dma_alloc_coherent(&v2ddev->pdev->dev, sizeof(uint32_t) * V2D_CMD_QUEUE_SIZE,
                                                &v2ddev->dev_cmd_queue, GFP_KERNEL);
    if (!v2ddev->virt_cmd_queue) {
        rc = -ENOMEM;
        goto err_dev_cmd_queue;
    }

    /* TODO: lock */
    minor = idr_alloc(&v2ddev_idr, v2ddev, 0, V2D_MAX_COUNT, GFP_KERNEL);
    if (IS_ERR_VALUE(minor)) {
        rc = minor;
        goto err_idr;
    }
    rc = cdev_add(&v2ddev->cdev, dev_base + minor, 1);
    if (IS_ERR_VALUE(rc))
        goto err_cdev;
    device = device_create(v2d_class, &dev->dev, v2ddev->cdev.dev, v2ddev, "v2d%d", minor);
    if (IS_ERR(device)) {
        rc = PTR_ERR(device);
        goto err_dev;
    }

    mutex_init(&v2ddev->dev_lock);
    mutex_init(&v2ddev->queue_lock);
    init_waitqueue_head(&v2ddev->write_queue);
    v2ddev->virt_cmd_queue[V2D_CMD_QUEUE_SIZE - 1] = VINTAGE2D_CMD_KIND_JUMP | ((v2ddev->dev_cmd_queue >> 2) << 2);
    reset(v2ddev, 1, 1, 1);
    set_write_ptr(v2ddev, v2ddev->dev_cmd_queue);
    set_read_ptr(v2ddev, v2ddev->dev_cmd_queue);
    v2ddev->head = v2ddev->tail = 0;
    v2ddev->space = V2D_CMD_QUEUE_SIZE - 2; // -JUMP and one free addr so as not to stop the dev from reading.
    v2ddev->last_user = NULL;
    v2ddev->counter = 1;
    v2ddev->last_counter_sync = 0;
    v2ddev->last_read_counter = 0;

    pci_set_drvdata(dev, v2ddev);

    iowrite32(VINTAGE2D_ENABLE_FETCH_CMD | VINTAGE2D_ENABLE_DRAW, v2ddev->bar0 + VINTAGE2D_ENABLE);
    iowrite32(VINTAGE2D_INTR_NOTIFY | VINTAGE2D_INTR_PAGE_FAULT | VINTAGE2D_INTR_INVALID_CMD |
              VINTAGE2D_INTR_FIFO_OVERFLOW | VINTAGE2D_INTR_CANVAS_OVERFLOW, v2ddev->bar0 + VINTAGE2D_INTR_ENABLE);
    return 0;

    device_destroy(v2d_class, v2ddev->cdev.dev);
err_dev:
    cdev_del(&v2ddev->cdev);
err_cdev:
    idr_remove(&v2ddev_idr, minor);
err_idr:
    dma_free_coherent(&v2ddev->pdev->dev, sizeof(uint32_t) * V2D_CMD_QUEUE_SIZE, v2ddev->virt_cmd_queue,
                      v2ddev->dev_cmd_queue);
err_dev_cmd_queue:
    dma_pool_destroy(v2ddev->canvas_pool);
err_canvas_pool:
    free_irq(dev->irq, v2ddev);
err_irq:
err_dma_mask:
    pci_iounmap(dev, v2ddev->bar0);
err_iomap:
    pci_release_regions(dev);
err_pci_region:
    pci_disable_device(dev);
err_pci_enable:
    kfree(v2ddev);
    return rc;
}

static void v2d_remove(struct pci_dev *dev) {
    struct v2d_data *v2ddev;

    v2ddev = pci_get_drvdata(dev);
    BUG_ON(!v2ddev);

    /* TODO: check for device users */

    iowrite32(0, v2ddev->bar0 + VINTAGE2D_ENABLE);  // Make sure the device is disabled.
    reset(v2ddev, 1, 1, 1);

    device_destroy(v2d_class, v2ddev->cdev.dev);
    cdev_del(&v2ddev->cdev);
    idr_remove(&v2ddev_idr, MINOR(v2ddev->cdev.dev));
    dma_pool_destroy(v2ddev->canvas_pool);
    dma_free_coherent(&v2ddev->pdev->dev, sizeof(uint32_t) * V2D_CMD_QUEUE_SIZE, v2ddev->virt_cmd_queue,
                      v2ddev->dev_cmd_queue);
    free_irq(dev->irq, v2ddev);
    pci_iounmap(dev, v2ddev->bar0);
    pci_release_regions(dev);
    pci_disable_device(dev);

    kfree(v2ddev);
}

DEFINE_PCI_DEVICE_TABLE(v2d_id_table) = {
    { PCI_DEVICE(VINTAGE2D_VENDOR_ID, VINTAGE2D_DEVICE_ID) },
    { 0 }
};

static struct pci_driver v2d_driver = {
    .name = "v2ddrv",
    .id_table = v2d_id_table,
    .probe = v2d_probe,
    .remove = v2d_remove
};

static int v2d_init(void) {
    int rc = -EINVAL;

    v2d_class = class_create(THIS_MODULE, "v2ddev");
    if (IS_ERR(v2d_class))
        return PTR_ERR(v2d_class);

    rc = alloc_chrdev_region(&dev_base, 0, V2D_MAX_COUNT, "v2ddev");
    if (IS_ERR_VALUE(rc)) {
        goto err_class;
    }

    rc = pci_register_driver(&v2d_driver);
    if (IS_ERR_VALUE(rc)) {
        goto err_chrdev;
    }
    return 0;

err_chrdev:
    unregister_chrdev_region(dev_base, V2D_MAX_COUNT);
err_class:
    class_destroy(v2d_class);
    return rc;
}

static void v2d_cleanup(void) {
    pci_unregister_driver(&v2d_driver);
    unregister_chrdev_region(dev_base, V2D_MAX_COUNT);
    class_destroy(v2d_class);
    return;
}

module_init(v2d_init);
module_exit(v2d_cleanup);

MODULE_LICENSE("GPL");