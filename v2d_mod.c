/* Tomasz Zakrzewski, tz336079      /
 * ZSO 2015/2016, Vintage2D driver */
// Kernel includes
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "vintage2d.h"
#include "v2d_ioctl.h"

MODULE_LICENSE("GPL");

#define MAX_SIZE 128*1024

static int v2d_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    printk(KERN_WARNING "v2d probe...\n");
    return 0;
}

void v2d_remove(struct pci_dev *dev) {
    // TODO
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

static int v2d_init(void)
{
    printk(KERN_WARNING "Init: TODO\n");
    return pci_register_driver(&v2d_driver);
}

static void v2d_cleanup(void)
{
    printk(KERN_WARNING "v2d stopping for device: TODO");
    pci_unregister_driver(&v2d_driver);
    return;
}

module_init(v2d_init);
module_exit(v2d_cleanup);