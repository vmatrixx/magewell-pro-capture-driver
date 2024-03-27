////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing) 
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <media/v4l2-ioctl.h>

#include "mw-event-dev.h"
#include "mw-event-ioctl-priv.h"

#define DEVICE_COUNT    (1)

#define MW_EVENT_NAME "mwevent"
#define DEV_NAME_BASE "mw-event"

struct mw_event_dev {
    struct device       *dev;
    struct cdev         cdev;

    int                 c_major;
    struct class        *c_class;

    os_spinlock_t       fh_lock;
    struct list_head    fh_list;
};

struct mw_event_fh  {
    struct list_head    list_node;

    /* sync for close */
    struct rw_semaphore io_sem;
    struct mw_event_io  mwev;
};

static struct mw_event_dev g_dev;

static ssize_t mw_event_dev_read(struct file *filp, char __user *buf,
        size_t sz, loff_t *off)
{
    return 0;
}

static ssize_t mw_event_dev_write(struct file *filp, const char __user *buf,
        size_t sz, loff_t *off)
{
    return 0;
}

static inline long __ioctl_wrapper(struct file *file, unsigned int cmd, void *arg)
{
    int ret;
    struct mw_event_fh *fh = (struct mw_event_fh *)file->private_data;

    ret = mw_event_ioctl(&fh->mwev, cmd, arg);

    return ret;
}

static long mw_event_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret;
    struct mw_event_fh *fh = (struct mw_event_fh *)filp->private_data;

    down_read(&fh->io_sem);
    ret = os_args_usercopy(filp, cmd, arg, __ioctl_wrapper);
    up_read(&fh->io_sem);

    return ret;
}

static int mw_event_dev_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct mw_event_fh *fh = NULL;

    fh = (struct mw_event_fh *)os_zalloc(sizeof(struct mw_event_fh));
    if (fh == NULL)
        return OS_RETURN_NOMEM;

    ret = mw_event_ioctl_init(&fh->mwev);
    if (ret != OS_RETURN_SUCCESS)
        goto init_err;

    init_rwsem(&fh->io_sem);

    os_spin_lock(g_dev.fh_lock);
    list_add_tail(&fh->list_node, &g_dev.fh_list);
    os_spin_unlock(g_dev.fh_lock);

    filp->private_data = fh;

    return OS_RETURN_SUCCESS;

init_err:
    os_free(fh);
    return ret;
}

static int mw_event_dev_release(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct mw_event_fh *fh = (struct mw_event_fh *)filp->private_data;

    down_write(&fh->io_sem);

    os_spin_lock(g_dev.fh_lock);
    list_del_init(&fh->list_node);
    os_spin_unlock(g_dev.fh_lock);

    mw_event_ioctl_deinit(&fh->mwev);

    os_free(fh);

    // We can't call this here as fh is freed.
    //up_write(&fh->io_sem);

    return ret;
}

static const struct file_operations mw_event_dev_fops =
{
    .owner   = THIS_MODULE,
    .open    = mw_event_dev_open,
    .read    = mw_event_dev_read,
    .write   = mw_event_dev_write,
    .release = mw_event_dev_release,
    .unlocked_ioctl	= mw_event_dev_ioctl,
    .compat_ioctl	= mw_event_dev_ioctl,
    .llseek  = no_llseek,
};

int mw_event_dev_create(void)
{
    int ret;
    dev_t dev_id;
    int minor = 0;

    g_dev.fh_lock = os_spin_lock_alloc();
    if (g_dev.fh_lock == NULL)
        return -ENOMEM;

    INIT_LIST_HEAD(&g_dev.fh_list);

    ret = alloc_chrdev_region(&dev_id, minor, DEVICE_COUNT, MW_EVENT_NAME);
    if (ret < 0) {
        printk(KERN_ERR "%s: can't get major number\n", __func__);
        goto out;
    }

    g_dev.c_major = MAJOR(dev_id);

    g_dev.c_class = class_create(THIS_MODULE, MW_EVENT_NAME);
    if (IS_ERR(g_dev.c_class)) {
        ret = PTR_ERR(g_dev.c_class);
        g_dev.c_class = NULL;
        goto class_err;
    }

    cdev_init(&g_dev.cdev, &mw_event_dev_fops);
    ret = cdev_add(&g_dev.cdev, MKDEV(g_dev.c_major, minor), DEVICE_COUNT);
    if (ret < 0) {
        printk(KERN_ERR "%s: cdev_add failed\n", __func__);
        goto cdev_err;
    }

    g_dev.dev = device_create(g_dev.c_class, NULL, dev_id, NULL, DEV_NAME_BASE);
    if(IS_ERR(g_dev.dev)){
        printk("device_create failed!\n");
        ret = PTR_ERR(g_dev.dev);
        g_dev.dev = NULL;
        goto device_err;
    }

    return 0;

device_err:
    cdev_del(&g_dev.cdev);
cdev_err:
    class_destroy(g_dev.c_class);
    g_dev.c_class = NULL;
class_err:
    unregister_chrdev_region(dev_id, DEVICE_COUNT);
    g_dev.c_major = 0;
out:
    return ret;
}

void mw_event_dev_destroy(void)
{
    if (g_dev.dev != NULL) {
        device_destroy(g_dev.c_class, g_dev.dev->devt);
        g_dev.dev = NULL;
    }
    cdev_del(&g_dev.cdev);
    if (g_dev.c_class != NULL) {
        class_destroy(g_dev.c_class);
        g_dev.c_class = NULL;
    }

    if (g_dev.c_major != 0) {
        dev_t dev_id = MKDEV(g_dev.c_major, 0);
        unregister_chrdev_region(dev_id, DEVICE_COUNT);
    }
    if (g_dev.fh_lock != NULL) {
        os_spin_lock_free(g_dev.fh_lock);
        g_dev.fh_lock = NULL;
    }
}

bool mw_event_is_event_valid(os_event_t event)
{
    bool ret = false;
    struct mw_event_fh *fh = NULL;

    os_spin_lock(g_dev.fh_lock);
    if (!list_empty(&g_dev.fh_list)) {
        list_for_each_entry(fh, &g_dev.fh_list, list_node) {
            if (fh != NULL) {
                os_spin_lock(fh->mwev.event_lock);
                ret = mw_event_is_event_valid_nolock(&fh->mwev, event);
                os_spin_unlock(fh->mwev.event_lock);
                if (ret)
                    break;
            }
        }
    }
    os_spin_unlock(g_dev.fh_lock);

    return ret;
}
