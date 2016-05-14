/* Tomasz Zakrzewski, tz336079      /
 * ZSO 2015/2016, Vintage2D driver */
// Kernel includes
#include <linux/module.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define MAX_SIZE 128*1024

static int v2d_init_module(void)
{
    printk(KERN_WARNING "Init: TODO");

    return 0;
}

static void v2d_cleanup_module(void)
{
    printk(KERN_WARNING "v2d stopping for device: TODO");
    return;
}

module_init(v2d_init_module);
module_exit(v2d_cleanup_module);