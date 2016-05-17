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

#include "vintage2d.h"
#include "v2d_ioctl.h"

MODULE_LICENSE("GPL");

#define V2D_MAX_COUNT 256

static struct class *v2d_class = NULL;
dev_t dev_base = 0;
static DEFINE_IDR(v2ddev_idr);

struct v2d_data {
    struct pci_dev *pdev;
    struct cdev cdev;
    void __iomem *bar0;
};

static irqreturn_t v2d_irq(int irq, void *dev) {
    return IRQ_HANDLED;
}

static int v2d_open(struct inode *i, struct file *f) {
    return 0;
}

static int v2d_release(struct inode *i, struct file *f) {
    return 0;
}

static ssize_t v2d_read(struct file *f, char __user *buf, size_t size, loff_t *off) {
    return 0;
}

static ssize_t v2d_write(struct file *f, const char __user *buf, size_t size, loff_t *off) {
    return 0;
}

static long v2d_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    return 0;
}

static int v2d_mmap(struct file *f, struct vm_area_struct *vm_area) {
    return 0;
}

static int v2d_fsync(struct file *f, loff_t a, loff_t b, int datasync) {
    return 0;
}

static struct file_operations v2d_fops = {
    .owner = THIS_MODULE,
    .open = v2d_open,
    .release = v2d_release,
    .read = v2d_read,
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
    if (IS_ERR(v2ddev->bar0)) {
        rc = PTR_ERR(v2ddev->bar0);
        goto err_iomap;
    }

    pci_set_master(dev);
    rc = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
    if (IS_ERR_VALUE(rc)) {
        goto err_dma_mask;
    }

    rc = request_irq(dev->irq, v2d_irq, IRQF_SHARED, "v2ddev", v2ddev);
    if (IS_ERR_VALUE(rc))
        goto err_irq;

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

    pci_set_drvdata(dev, v2ddev);
    return 0;

    device_destroy(v2d_class, v2ddev->cdev.dev);
err_dev:
    cdev_del(&v2ddev->cdev);
err_cdev:
    idr_remove(&v2ddev_idr, minor);
err_idr:
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

void v2d_remove(struct pci_dev *dev) {
    struct v2d_data *v2ddev;

    v2ddev = pci_get_drvdata(dev);
    BUG_ON(!v2ddev);

    /* TODO: check for device users */

    iowrite32(0, v2ddev->bar0 + VINTAGE2D_ENABLE);
    iowrite32(0, v2ddev->bar0 + VINTAGE2D_INTR_ENABLE);

    device_destroy(v2d_class, v2ddev->cdev.dev);
    cdev_del(&v2ddev->cdev);
    idr_remove(&v2ddev_idr, MINOR(v2ddev->cdev.dev));
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