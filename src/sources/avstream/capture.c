////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/version.h>

#include <xi-version.h>

#include "dma/mw-dma-mem-priv.h"

#include "ospi/linux-file.h"

#include "xi-driver.h"
#include "ospi/ospi.h"
#include "v4l2.h"
#include "alsa.h"
#include "capture.h"
#include "mw-event-dev.h"

extern void parse_internal_params(struct xi_driver *driver, const char *internal_params);

// TODO
enum XI_CAP_PCI_ID {
    XI_CAP_PCI_VENDOR_ID                = 0x1CD7,

    XI_CAP_PCI_DEVICE_ID1               = 0x0002, /* AIO */
    XI_CAP_PCI_DEVICE_ID2               = 0x0003, /* DVI */
    XI_CAP_PCI_DEVICE_ID3               = 0x0004, /* HDMI */
    XI_CAP_PCI_DEVICE_ID4               = 0x0005, /* SDI */
    XI_CAP_PCI_DEVICE_ID5               = 0x0006, /* DualSDI */
    XI_CAP_PCI_DEVICE_ID6               = 0x0007, /* DualDVI */
    XI_CAP_PCI_DEVICE_ID7               = 0x0008, /* DualHDMI */
    XI_CAP_PCI_DEVICE_ID8               = 0x0009, /* QuadSDI */
    XI_CAP_PCI_DEVICE_ID9               = 0x0010, /* QuadHDMI */
    XI_CAP_PCI_DEVICE_ID10              = 0x0011, /* MiniHDMI */
    XI_CAP_PCI_DEVICE_ID11              = 0x0012, /* HDMI 4k */
    XI_CAP_PCI_DEVICE_ID12              = 0x0013, /* MiniSDI */
    XI_CAP_PCI_DEVICE_ID13              = 0x0014, /* AIO 4K+ */
    XI_CAP_PCI_DEVICE_ID14              = 0x0015, /* HDMI 4K+ */
    XI_CAP_PCI_DEVICE_ID15              = 0x0016, /* DVI 4K */
    XI_CAP_PCI_DEVICE_ID16              = 0x0017, /* AIO 4K */
    XI_CAP_PCI_DEVICE_ID17              = 0x0018, /* SDI 4K+ */
    XI_CAP_PCI_DEVICE_ID18              = 0x0025, /* Hexa CVBS */
    XI_CAP_PCI_DEVICE_ID19              = 0x0026, /* Dual HDMI 4K+ */
    XI_CAP_PCI_DEVICE_ID20              = 0x0027, /* Dual SDI 4K+ */
    XI_CAP_PCI_DEVICE_ID21              = 0x0024, /* HDMI 4K+ TBT */
    XI_CAP_PCI_DEVICE_ID22              = 0x0023, /* Dual AIO */
    XI_CAP_PCI_DEVICE_ID23              = 0x0028, /* EZ100A */
};

module_param(debug_level, uint, 00644);
MODULE_PARM_DESC(debug_level, "enable debug messages");

static char *nosignal_file = NULL;
module_param(nosignal_file, charp, 00644);
MODULE_PARM_DESC(nosignal_file, "Display this picture when there is no input signal");

static char *unsupported_file = NULL;
module_param(unsupported_file, charp, 00644);
MODULE_PARM_DESC(unsupported_file, "Display this picture when the input signal is not supported");

static char *locking_file = NULL;
module_param(locking_file, charp, 00644);
MODULE_PARM_DESC(locking_file, "Display this picture when the input signal is locking");

static bool init_switch_eeprom = false;
module_param(init_switch_eeprom, bool, 00644);
MODULE_PARM_DESC(init_switch_eeprom, "Force init switch eeprom");

static bool disable_msi = false;
module_param(disable_msi, bool, 00644);
MODULE_PARM_DESC(disable_msi, "Disable or enable message signaled interrupts");

unsigned int enum_frameinterval_min = 166666;
module_param(enum_frameinterval_min, uint, 00644);
MODULE_PARM_DESC(enum_frameinterval_min, "Min frame interval for VIDIOC_ENUM_FRAMEINTERVALS (default: 166666(100ns))");

unsigned int enum_framesizes_type = 0;
module_param(enum_framesizes_type, uint, 00644);
MODULE_PARM_DESC(enum_framesizes_type, "VIDIOC_ENUM_FRAMESIZES type (1: DISCRETE; 2: STEPWISE; otherwise: CONTINUOUS )");

static char *internal_params = NULL;
module_param(internal_params, charp, 00644);
MODULE_PARM_DESC(internal_params, "Parameters for internal usage");

static unsigned int edid_mode = 4;
module_param(edid_mode, uint, 00644);
MODULE_PARM_DESC(edid_mode, "EDID mode for the card with HDMI loop through port");

struct xi_cap_context {
    void __iomem            *reg_mem; /* pointer to mapped registers memory */
    struct xi_driver        driver;
    struct xi_v4l2_dev      v4l2;
    struct snd_card         *card[MAX_CHANNELS_PER_DEVICE];

    struct tasklet_struct   irq_tasklet;

    bool                    msi_enabled;
};

static const struct pci_device_id xi_cap_pci_tbl[] = {
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID1) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID2) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID3) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID4) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID5) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID6) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID7) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID8) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID9) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID10) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID11) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID12) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID13) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID14) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID15) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID16) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID17) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID18) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID19) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID20) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID21) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID22) },
    { PCI_DEVICE(XI_CAP_PCI_VENDOR_ID, XI_CAP_PCI_DEVICE_ID23) },
    { 0, }
};

static irqreturn_t xi_cap_irq_handler(int irq, void *data)
{
    struct xi_cap_context *ctx = data;

    if (xi_driver_irq_process_top(&ctx->driver) != 0)
        return IRQ_HANDLED;

    tasklet_schedule(&ctx->irq_tasklet);
    return IRQ_HANDLED;
}

static void xi_cap_irq_tasklet(unsigned long data)
{
    struct xi_cap_context *ctx = (struct xi_cap_context *)data;

    xi_driver_irq_process_bottom(&ctx->driver);
}


int loadPngImage(const char *filename, png_pix_info_t *dinfo)
{
    struct file *fp = NULL;
    struct kstat stat;
    int ret = 0;
    void *filebuf = NULL;
    size_t total_read = 0;
    size_t one_read = 0;

    fp = linux_file_open(filename, O_RDONLY, 0);
    if (IS_ERR_OR_NULL(fp))
        return IS_ERR(fp) ? PTR_ERR(fp) : -1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
	ret = vfs_getattr(&fp->f_path, &stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
	ret = vfs_getattr(&fp->f_path, &stat);
#else
    ret = vfs_getattr(fp->f_path.mnt, fp->f_path.dentry, &stat);
#endif
    if (ret != 0) {
        goto failed;
    }

    filebuf = os_malloc(stat.size);

    if (filebuf == NULL) {
        ret = -ENOMEM;
        goto failed;
    }

    while (total_read < stat.size) {
        size_t want = (stat.size - total_read) > PAGE_SIZE ? PAGE_SIZE : (stat.size - total_read);
        one_read = linux_file_read(fp, total_read, filebuf+total_read, want);
        if (one_read <= 0)
            break;

        total_read += one_read;
    }

    if (total_read != stat.size) {
        ret = -EIO;
        goto failed;
    }

    ret = loadEmbeddedPngImage(filebuf, total_read, dinfo);

failed:
    if (filebuf != NULL) {
        os_free(filebuf);
    }
    linux_file_close(fp);

    return ret;
}

static struct pci_dev *__pci_get_bridge_device(struct pci_dev *pdev)
{
    struct pci_dev *rdev = NULL;

    rdev = pci_physfn(pdev);
    if (pci_is_root_bus(rdev->bus))
        return NULL;

    return rdev->bus->self;
}

static struct pci_dev *__pci_get_parent_bridge(struct pci_dev *pdev)
{
    struct pci_dev *rdev = NULL;

    if (!init_switch_eeprom)
        return NULL;

    rdev = pci_physfn(pdev);
    if (pci_is_root_bus(rdev->bus->parent))
        return NULL;

    return rdev->bus->parent->self;
}

static int xi_cap_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int ret;
    struct xi_cap_context *ctx;
    png_pix_info_t nosignal_png;
    png_pix_info_t unsupported_png;
    png_pix_info_t locking_png;
    struct parameter_t *param_mgr;
    int ch_count;
    int ch_index;

    ctx = kzalloc(sizeof(struct xi_cap_context), GFP_KERNEL);
    if (ctx == NULL) {
        xi_debug(0, "Error: Alloc memory for ctx failed\n");
        ret = -ENOMEM;
        goto ctx_err;
    }

    tasklet_init(&ctx->irq_tasklet, xi_cap_irq_tasklet, (unsigned long)ctx);

    /* Enable PCI */
    ret = pci_enable_device(pdev);
    if (ret) {
        xi_debug(0, "Error: Enable PCI Device\n");
        goto free_ctx;
    }

    pci_set_master(pdev);

    ret = pci_request_regions(pdev, VIDEO_CAP_DRIVER_NAME);
    if (ret < 0) {
        xi_debug(0, "Error: Request memory region failed: errno=%d\n", ret);
        goto disable_pci;
    }

    ctx->reg_mem = pci_iomap(pdev, 0, 0);
    if (NULL == ctx->reg_mem) {
        xi_debug(0, "Error: ioremap_nocache\n");
        ret = -ENOMEM;
        goto free_mem;
    }

    ctx->msi_enabled = false;
    if (!disable_msi && xi_driver_is_support_msi(ctx->reg_mem)) {
        ret = pci_enable_msi(pdev);
        if (ret == 0) {
            ctx->msi_enabled = true;
            xi_debug(0, "MSI Enabled\n");
        }
    }

    if ((sizeof(dma_addr_t) > 4) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0)
            !dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
#else
            !pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
#endif
        xi_debug(1, "dma 64 OK!\n");
    } else {
        xi_debug(1, "dma 64 not OK!\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0)
        if ((dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) < 0) &&
                (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) < 0) {
#else
        if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) < 0) &&
                (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) < 0) {
#endif
            xi_debug(0, "DMA configuration failed\n");
            goto disable_pci;
        }
    }

    pci_set_drvdata(pdev, ctx);

    ret = request_irq(pdev->irq,
            xi_cap_irq_handler,
            IRQF_SHARED,
            VIDEO_CAP_DRIVER_NAME,
            ctx);
    if (ret) {
        xi_debug(0, "Error: request_irq \n");
        pdev->irq = -1;
        goto free_io;
    }

    /* load png */
    memset(&nosignal_png, 0, sizeof(nosignal_png));
    memset(&unsupported_png, 0, sizeof(unsupported_png));
    memset(&locking_png, 0, sizeof(locking_png));
    if (nosignal_file != NULL)
        loadPngImage(nosignal_file, &nosignal_png);
    if (unsupported_file != NULL)
        loadPngImage(unsupported_file, &unsupported_png);
    if (locking_file != NULL)
        loadPngImage(locking_file, &locking_png);

    if (init_switch_eeprom && (__pci_get_parent_bridge(pdev) == NULL)) {
        xi_debug(0, "switch eeprom failed\n");
        goto free_irq;
    }

    ret = xi_driver_init(&ctx->driver, ctx->reg_mem, &pdev->dev,
                         &nosignal_png, &unsupported_png, &locking_png,
                         __pci_get_bridge_device(pdev),
                         __pci_get_parent_bridge(pdev));
    if (ret != 0) {
        goto free_irq;
    }

    param_mgr = xi_driver_get_parameter_manager(&ctx->driver);
    if (param_mgr != NULL)
        parameter_SetEDIDMode(param_mgr, edid_mode);

    parse_internal_params(&ctx->driver, internal_params);

    ret = xi_v4l2_init(&ctx->v4l2, &ctx->driver, &pdev->dev);
    if (ret != 0)
        goto v4l2_err;

    ch_count = xi_driver_get_channle_count(&ctx->driver);
    for (ch_index = 0; ch_index < ch_count; ch_index++) {
        ret = alsa_card_init(&ctx->driver, ch_index, &ctx->card[ch_index], &pdev->dev);
        if (ret != 0)
            goto alsa_err;
    }

    xi_debug(1, VIDEO_CAP_DRIVER_NAME " capture probe OK!\n");

    return 0;

alsa_err:
    for (ch_index = 0; ch_index < ch_count; ch_index++) {
        if (ctx->card[ch_index] != NULL) {
            alsa_card_release(ctx->card[ch_index]);
            ctx->card[ch_index] = NULL;
        }
    }
    xi_v4l2_release(&ctx->v4l2);
v4l2_err:
    xi_driver_deinit(&ctx->driver);
free_irq:
    free_irq(pdev->irq, ctx);
free_io:
    pci_iounmap(pdev, ctx->reg_mem);
free_mem:
    pci_release_regions(pdev);
disable_pci:
    if (ctx->msi_enabled)
        pci_disable_msi(pdev);
    pci_disable_device(pdev);
free_ctx:
    kfree(ctx);
ctx_err:
    return ret;
}

static void xi_cap_remove(struct pci_dev *pdev)
{
    struct xi_cap_context *ctx = pci_get_drvdata(pdev);
    int ch_count;
    int ch_index;

    xi_debug(0, "\n");

    if (ctx == NULL)
        return;

    ch_count = xi_driver_get_channle_count(&ctx->driver);
    for (ch_index = 0; ch_index < ch_count; ch_index++) {
        if (ctx->card[ch_index] != NULL) {
            alsa_card_release(ctx->card[ch_index]);
            ctx->card[ch_index] = NULL;
        }
    }
    xi_v4l2_release(&ctx->v4l2);

    xi_driver_deinit(&ctx->driver);

    if (pdev->irq > 0) {
        free_irq(pdev->irq, ctx);
    }

    if (ctx->msi_enabled)
        pci_disable_msi(pdev);

    if (NULL != ctx->reg_mem)
        pci_iounmap(pdev, ctx->reg_mem);

    pci_release_regions(pdev);

    pci_set_drvdata(pdev, NULL);

    pci_disable_device(pdev);

    kfree(ctx);
}

static void xi_cap_shutdown(struct pci_dev *pdev)
{
    struct xi_cap_context *ctx = pci_get_drvdata(pdev);

    xi_debug(0, "\n");

    xi_driver_shutdown(&ctx->driver);

    xi_cap_remove(pdev);
}

#ifdef CONFIG_PM
static int xi_cap_suspend(struct pci_dev *pdev, pm_message_t state)
{
    struct xi_cap_context *ctx = pci_get_drvdata(pdev);
    int ch_count;
    int ch_index;

    ch_count = xi_driver_get_channle_count(&ctx->driver);
    for (ch_index = 0; ch_index < ch_count; ch_index++) {
        alsa_suspend(ctx->card[ch_index]);
    }
    xi_v4l2_suspend(&ctx->v4l2);

    xi_driver_sleep(&ctx->driver);

    if (pdev->irq > 0) {
        free_irq(pdev->irq, ctx);
    }

    if (ctx->msi_enabled)
        pci_disable_msi(pdev);
    ctx->msi_enabled = false;

    pci_disable_device(pdev);
    pci_save_state(pdev);

    pci_set_power_state(pdev, pci_choose_state(pdev, state));

    return 0;
}

static int xi_cap_resume(struct pci_dev *pdev)
{
    struct xi_cap_context *ctx = pci_get_drvdata(pdev);
    int ret;
    png_pix_info_t nosignal_png;
    png_pix_info_t unsupported_png;
    png_pix_info_t locking_png;
    int ch_count;
    int ch_index;

    pci_set_power_state(pdev, PCI_D0);
    pci_restore_state(pdev);

    ret = pci_enable_device(pdev);
    if(ret) {
        xi_debug(0,"Error: for resume on\n");
        ret = -EIO;
        goto out;
    }

    ctx->msi_enabled = false;
    if (!disable_msi && xi_driver_is_support_msi(ctx->reg_mem)) {
        ret = pci_enable_msi(pdev);
        if (ret == 0) {
            ctx->msi_enabled = true;
            xi_debug(0, "MSI Enabled\n");
        }
    }

    ret = request_irq(pdev->irq,
            xi_cap_irq_handler,
            IRQF_SHARED,
            VIDEO_CAP_DRIVER_NAME,
            ctx);
    if (ret) {
        xi_debug(0, "Error: request_irq \n");
        pdev->irq = -1;
        goto out;
    }

    pci_set_master(pdev);

    /* load png */
    memset(&nosignal_png, 0, sizeof(nosignal_png));
    memset(&unsupported_png, 0, sizeof(unsupported_png));
    memset(&locking_png, 0, sizeof(locking_png));
    if (nosignal_file != NULL)
        loadPngImage(nosignal_file, &nosignal_png);
    if (nosignal_file != NULL)
        loadPngImage(unsupported_file, &unsupported_png);
    if (nosignal_file != NULL)
        loadPngImage(locking_file, &locking_png);

    xi_driver_resume(&ctx->driver,
                     &nosignal_png, &unsupported_png, &locking_png,
                     __pci_get_bridge_device(pdev),
                     NULL);

    ch_count = xi_driver_get_channle_count(&ctx->driver);
    for (ch_index = 0; ch_index < ch_count; ch_index++) {
        alsa_resume(ctx->card[ch_index]);
    }
    xi_v4l2_resume(&ctx->v4l2);

    return 0;

out:
    xi_cap_remove(pdev);
    return ret;
}

#endif

static struct pci_driver xi_cap_pci_driver = {
    .name            = VIDEO_CAP_DRIVER_NAME,
    .probe           = xi_cap_probe,
    .remove          = xi_cap_remove,
    .id_table        = xi_cap_pci_tbl,
    .shutdown        = xi_cap_shutdown,
#ifdef CONFIG_PM
    .suspend 		 = xi_cap_suspend,
    .resume 		 = xi_cap_resume,
#endif
};

static int __init xi_cap_init(void)
{
    int ret;

    ret = os_init();
    if (ret != OS_RETURN_SUCCESS)
        return ret;

    ret = mw_dma_memory_init();
    if (ret != OS_RETURN_SUCCESS)
        goto dma_err;

    ret = mw_event_dev_create();
    if (ret != 0)
        goto edev_err;

    ret = pci_register_driver(&xi_cap_pci_driver);
    if (ret != 0)
        goto pci_err;

    return 0;

pci_err:
    mw_event_dev_destroy();
edev_err:
    mw_dma_memory_deinit();
dma_err:
    os_deinit();
    return ret;
}

static void __exit xi_cap_cleanup(void)
{
    pci_unregister_driver(&xi_cap_pci_driver);

    mw_event_dev_destroy();
    mw_dma_memory_deinit();
    os_deinit();
}

module_init(xi_cap_init);
module_exit(xi_cap_cleanup);

MODULE_DESCRIPTION("Magewell Electronics Co., Ltd. Pro Capture Driver");
MODULE_AUTHOR("Magewell Electronics Co., Ltd. <support@magewell.net>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, xi_cap_pci_tbl);
MODULE_VERSION(VERSION_STRING);
