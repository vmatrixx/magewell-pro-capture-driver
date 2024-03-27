////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#include "ospi.h"

#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/freezer.h>
#include <linux/pci.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#endif

unsigned int debug_level = 0;

struct kmem_cache *g_spin_cache = NULL;
struct kmem_cache *g_mutex_cache = NULL;
struct kmem_cache *g_event_cache = NULL;

/* init */
int os_init(void)
{
    int ret;
    size_t size;

    /* kmem_cache_create require size >= BYTES_PER_WORD && size <= KMALLOC_MAX_SIZE */
    size = sizeof(spinlock_t) < sizeof(void *) ? sizeof(void *) : sizeof(spinlock_t);
    g_spin_cache = kmem_cache_create("mw-spin", size,
                                     0, 0, NULL);
    if (g_spin_cache == NULL)
        return OS_RETURN_NOMEM;

    g_mutex_cache = kmem_cache_create("mw-mutex", sizeof(struct mutex),
                                     0, 0, NULL);
    if (g_mutex_cache == NULL) {
        ret = OS_RETURN_NOMEM;
        goto mutex_err;
    }

    g_event_cache = kmem_cache_create("mw-event", sizeof(struct _os_event_t),
                                     0, 0, NULL);
    if (g_event_cache == NULL) {
        ret = OS_RETURN_NOMEM;
        goto event_err;
    }

    return OS_RETURN_SUCCESS;

event_err:
    kmem_cache_destroy(g_mutex_cache);
mutex_err:
    kmem_cache_destroy(g_spin_cache);
    return ret;
}

void os_deinit(void)
{
    if (g_spin_cache != NULL)
        kmem_cache_destroy(g_spin_cache);
    if (g_mutex_cache != NULL)
        kmem_cache_destroy(g_mutex_cache);
    if (g_event_cache != NULL)
        kmem_cache_destroy(g_event_cache);

    g_spin_cache = NULL;
    g_mutex_cache = NULL;
    g_event_cache = NULL;
}


/* print */
int __os_printf(const char *s, ...)
{
    int retval;
    va_list ap;

    va_start(ap, s);
    retval = vprintk(s, ap);
    va_end(ap);

    return retval;
}


/* time */
os_clock_kick_t os_get_clock_kick(void)
{
    return jiffies;
}

uint64_t os_clock_kick_to_msecs(os_clock_kick_t kick)
{
    return jiffies_to_msecs(kick);
}

os_clock_kick_t os_msecs_to_clock_kick(uint64_t msecs)
{
    return msecs_to_jiffies(msecs);
}

/* time_after(a,b) returns true if the time a is after time b. */
int os_clock_kick_after(uint64_t a, uint64_t b)
{
    return time_after((unsigned long)a, (unsigned long)b);
}

int os_clock_kick_after_eq(uint64_t a, uint64_t b)
{
    return time_after_eq((unsigned long)a, (unsigned long)b);
}


/* timer */
struct _os_timer {
    struct timer_list   k_timer;
    timeout_func_t      func;
    void *              param;
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
static void __os_timer_callback_wrapper(unsigned long param)
{
    os_timer timer = (os_timer)param;

    if (timer && timer->func)
        timer->func(timer->param);
}
#else
static void __os_timer_callback_wrapper(struct timer_list *t)
{
    os_timer timer = from_timer(timer, t, k_timer);

    if (timer && timer->func)
        timer->func(timer->param);
}
#endif

os_timer os_timer_alloc(timeout_func_t timeout_func, void *param)
{
    os_timer timer;

    timer = kzalloc(sizeof(*timer), GFP_KERNEL);
    if (timer == NULL)
        return NULL;

    timer->func = timeout_func;
    timer->param = param;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
    setup_timer(&timer->k_timer, __os_timer_callback_wrapper, (unsigned long)timer);
#else
    timer_setup(&timer->k_timer, __os_timer_callback_wrapper, 0);
#endif

    return timer;
}

void os_timer_free(os_timer timer)
{
    del_timer(&timer->k_timer);
    kfree(timer);
}

void os_timer_schedule_clock_kick(os_timer timer, os_clock_kick_t kick)
{
    mod_timer(&timer->k_timer, kick);
}

void os_timer_schedule_relative(os_timer timer, uint32_t timeout_in_ms)
{
    os_clock_kick_t _time = msecs_to_jiffies(timeout_in_ms);

    mod_timer(&timer->k_timer, jiffies+_time);
}

void os_timer_cancel(os_timer timer)
{
    del_timer(&timer->k_timer);
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
void atomic_or(int i, atomic_t *v)
{
    int old;
    int new;

    do {
        old = atomic_read(v);
        new = old | i;
    } while (atomic_cmpxchg(v, old, new) != old);
}
#endif

int os_atomic_read(const os_atomic_t *v)
{
    return atomic_read(v);
}

void os_atomic_set(os_atomic_t *v, int i)
{
    atomic_set(v, i);
}

void os_atomic_inc(os_atomic_t *v)
{
    atomic_inc(v);
}

void os_atomic_dec(os_atomic_t *v)
{
    atomic_dec(v);
}

void os_atomic_add(os_atomic_t *v, int i)
{
    atomic_add(i, v);
}

void os_atomic_sub(os_atomic_t *v, int i)
{
    atomic_sub(i, v);
}

void os_atomic_or(os_atomic_t *v, int i)
{
    atomic_or(i, v);
}

int os_atomic_xchg(os_atomic_t *v, int i)
{
    return atomic_xchg(v, i);
}

int os_atomic_cmpxchg(os_atomic_t *v, int oldv, int newv)
{
    return atomic_cmpxchg(v, oldv, newv);
}

void os_clear_bit(int nr, volatile unsigned long *addr)
{
    clear_bit(nr, addr);
}

void os_set_bit(int nr, volatile unsigned long *addr)
{
    set_bit(nr, addr);
}

int os_test_bit(int nr, const volatile unsigned long *addr)
{
    return test_bit(nr, addr);
}

int os_test_and_set_bit(int nr, volatile unsigned long *addr)
{
    return test_and_set_bit(nr, addr);
}

int os_test_and_clear_bit(int nr, volatile unsigned long *addr)
{
    return test_and_clear_bit(nr, addr);
}


/* spin lock */
os_spinlock_t os_spin_lock_alloc(void)
{
    os_spinlock_t lock;

    lock = kmem_cache_alloc(g_spin_cache, GFP_KERNEL);
    if (lock == NULL)
        return NULL;

    spin_lock_init((spinlock_t *)lock);

    return lock;
}

void os_spin_lock_free(os_spinlock_t lock)
{
    kmem_cache_free(g_spin_cache, lock);
}

void os_spin_lock(os_spinlock_t lock)
{
    spin_lock(lock);
}

void os_spin_unlock(os_spinlock_t lock)
{
    spin_unlock(lock);
}

void os_spin_lock_bh(os_spinlock_t lock)
{
    spin_lock_bh(lock);
}

void os_spin_unlock_bh(os_spinlock_t lock)
{
    spin_unlock_bh(lock);
}

int os_spin_try_lock(os_spinlock_t lock)
{
    return spin_trylock(lock);
}

/* mutex */
os_mutex_t os_mutex_alloc(void)
{
    os_mutex_t lock;

    lock = kmem_cache_alloc(g_mutex_cache, GFP_KERNEL);
    if (lock == NULL)
        return NULL;

    mutex_init(lock);

    return lock;
}

void os_mutex_free(os_mutex_t lock)
{
    kmem_cache_free(g_mutex_cache, lock);
}

void os_mutex_lock(os_mutex_t lock)
{
    mutex_lock(lock);
}

void os_mutex_unlock(os_mutex_t lock)
{
    mutex_unlock(lock);
}

int os_mutex_try_lock(os_mutex_t lock)
{
    return mutex_trylock(lock);
}

/* event */
os_event_t os_event_alloc(void)
{
    os_event_t event;

    event = kmem_cache_alloc(g_event_cache, GFP_KERNEL);
    if (event == NULL)
        return NULL;

    init_completion(&event->done);

    return event;
}

void os_event_free(os_event_t event)
{
    kmem_cache_free(g_event_cache, event);
}

void os_event_set(os_event_t event)
{
    complete(&event->done);
}

void os_event_clear(os_event_t event)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
#  define reinit_completion(x) INIT_COMPLETION(*(x))
#endif
    reinit_completion(&event->done);
}

bool os_event_is_set(os_event_t event)
{
    return completion_done(&event->done);
}

bool os_event_try_wait(os_event_t event)
{
    bool ret;
    bool real_ret = false;

    do {
        ret = try_wait_for_completion(&event->done);
        if (ret)
            real_ret = ret;
    } while (ret != false);

    return real_ret;
}

/* @timeout: -1: wait for ever; 0: no wait; >0: wait for @timeout ms */
/* ret 0: timeout, <0 err, >0 event occur */
int32_t os_event_wait(os_event_t event, int32_t timeout)
{
    unsigned long expire;
    long ret = 0;

    if (timeout < 0)
        expire = MAX_SCHEDULE_TIMEOUT;
    else
        expire = msecs_to_jiffies(timeout);

    ret = wait_for_completion_killable_timeout(&event->done, expire);

    // clear event
    while(ret > 0 && try_wait_for_completion(&event->done)) {}

    if (ret > INT_MAX)
        ret = 1;

    return ret;
}

/* @timeout: -1: wait for ever; 0: no wait; >0: wait for @timeout ms */
/* @ret: 0 timeout; >0 event occur */
int32_t os_event_wait_for_multiple(os_event_t events[], int num_events,
                                   int32_t timeout/* ms */)
{
    unsigned long expire;
    int i;
    bool got_event;
    int32_t ret = 0;

    if (timeout < 0)
        expire = INT_MAX; // MAX_SCHEDULE_TIMEOUT
    else
        expire = msecs_to_jiffies(timeout);

    for (i = 0; i < num_events; i++) {
#if (defined(CONFIG_PREEMPT_RT_FULL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)) \
    || LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0) \
    || (defined(CONFIG_PREEMPT_RT) && LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)) \
    || defined(OS_RHEL_8_4)
        events[i]->waitq.task = current;
        INIT_LIST_HEAD(&events[i]->waitq.task_list);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0) && !defined(OS_RHEL_8_4))
        prepare_to_swait(&events[i]->done.wait, &events[i]->waitq, TASK_KILLABLE);
#else
        prepare_to_swait_exclusive(&events[i]->done.wait, &events[i]->waitq, TASK_KILLABLE);
#endif
#else
        init_waitqueue_entry(&events[i]->waitq, current);
        /* add ourselves into wait queue */
        add_wait_queue(&events[i]->done.wait, &events[i]->waitq);
#endif
    }

    got_event = false;
    for (;;) {
        set_current_state(TASK_KILLABLE);

        for (i = 0; i < num_events; i++) {
            if (os_event_is_set(events[i])) {
                got_event = true;
                break;
            }
        }
        if (got_event) {
            ret = 1;
            break;
        }
        expire = schedule_timeout(expire);
        if (!expire) {
            ret = 0;
            break;
        }
    }

#if (defined(CONFIG_PREEMPT_RT_FULL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)) \
    || LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0) \
    || (defined(CONFIG_PREEMPT_RT) && LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)) \
    || defined(OS_RHEL_8_4)
    for (i = 0; i < num_events; i++) {
        finish_swait(&events[i]->done.wait, &events[i]->waitq);
    }
#else
    __set_current_state(TASK_RUNNING);
    for (i = 0; i < num_events; i++) {
        remove_wait_queue(&events[i]->done.wait, &events[i]->waitq);
    }
#endif

    return ret;
}


/* thread */
struct _os_thread_t {
    struct task_struct          *kthread;
    os_event_t                  wait_exit;
};

os_thread_t os_thread_create_and_run(int (*threadfn)(void *data),
                                     void *data,
                                     const char *name)
{
    os_thread_t thread = (os_thread_t)os_zalloc(sizeof(*thread));

    if (thread == NULL)
        return NULL;

    thread->wait_exit = os_event_alloc();
    if (thread->wait_exit == NULL) {
        os_free(thread);
        return NULL;
    }

    thread->kthread = kthread_run(threadfn, data, name);
    if (IS_ERR_OR_NULL(thread->kthread)) {
        os_event_free(thread->wait_exit);
        os_free(thread);
        return NULL;
    }

    return thread;
}

void os_thread_terminate_self(os_thread_t thread)
{
    os_event_set(thread->wait_exit);
}

void os_thread_wait_and_free(os_thread_t thread)
{
    os_event_wait(thread->wait_exit, -1);
    os_event_free(thread->wait_exit);
    os_free(thread);
}


/* mem io */
uint32_t pci_read_reg32(volatile void *addr) {
    return readl(addr);
}

void pci_write_reg32(volatile void *addr, uint32_t val) {
    writel(val, addr);
}


/* memory allocation */
struct _mhead {
    union {
        struct {
            unsigned char   mtype;
            size_t          msize;
        };
        unsigned char       pad[16]; // 4k plus dma need align to 16 bytes
    };
    char            dat[0];
};

void *os_malloc(size_t size)
{
    struct _mhead * mem = NULL;
    size_t          memsize = sizeof (*mem) + size;
    unsigned char   mtype;
    unsigned long   kmalloc_max_size;
    
    if (size == 0) {
        return (0);
    }

#ifdef KMALLOC_MAX_CACHE_SIZE
    kmalloc_max_size = KMALLOC_MAX_CACHE_SIZE;
#else
    kmalloc_max_size = PAGE_SIZE;
#endif

    if (memsize <= kmalloc_max_size) {
        mem = (struct _mhead *)kmalloc(memsize, GFP_KERNEL);
        mtype = OS_LINUX_MEM_TYPE_KMALLOC;
    }

    if (mem == NULL) {
        memsize = PAGE_ALIGN(memsize);
        mem = (struct _mhead *)vmalloc(memsize);
        mtype = OS_LINUX_MEM_TYPE_VMALLOC;
    }

    if (!mem) {
        return (0);
    }
    
    mem->mtype = mtype;
    mem->msize = memsize;
    
    return mem->dat;
}

void *os_zalloc(size_t size)
{
    struct _mhead * mem = NULL;
    size_t          memsize = sizeof (*mem) + size;
    unsigned char   mtype;
    unsigned long   kmalloc_max_size;

    if (size == 0) {
        return (0);
    }

#ifdef KMALLOC_MAX_CACHE_SIZE
    kmalloc_max_size = KMALLOC_MAX_CACHE_SIZE;
#else
    kmalloc_max_size = PAGE_SIZE;
#endif

    if (memsize <= kmalloc_max_size) {
        mem = (struct _mhead *)kzalloc(memsize, GFP_KERNEL);
        mtype = OS_LINUX_MEM_TYPE_KMALLOC;
    }

    if (mem == NULL) {
        memsize = PAGE_ALIGN(memsize);
        mem = (struct _mhead *)vmalloc(memsize);
        if (mem != NULL) {
            memset(mem, 0, memsize);
        }
        mtype = OS_LINUX_MEM_TYPE_VMALLOC;
    }

    if (!mem) {
        return (0);
    }

    mem->mtype = mtype;
    mem->msize = memsize;

    return mem->dat;
}

void os_free(void *address)
{
    struct _mhead * hdr;

    hdr = (struct _mhead *)address;
    hdr--;

    if (hdr->mtype == OS_LINUX_MEM_TYPE_KMALLOC)
        kfree(hdr);
    else
        vfree(hdr);
}

size_t os_mem_get_actual_size(void *address)
{
    struct _mhead * hdr;

    hdr = (struct _mhead *)address;
    hdr--;

    if (hdr->mtype == OS_LINUX_MEM_TYPE_KMALLOC)
        return ksize(hdr) - sizeof (*hdr);
    else
        return hdr->msize - sizeof (*hdr);
}

int os_mem_get_mem_type(void *address)
{
    struct _mhead * hdr;

    hdr = (struct _mhead *)address;
    hdr--;

    return hdr->mtype;
}


struct _os_contig_dma_desc_t {
    dma_addr_t          phys_addr;
    void                *virt_addr;
    size_t              size;

    void                *parent;
};

os_contig_dma_desc_t os_contig_dma_alloc(size_t size, os_dma_par_t par)
{
    int nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
    void *parent = par;
    os_contig_dma_desc_t desc;

    desc = os_zalloc(sizeof(*desc));
    if (desc == NULL)
        return NULL;

    desc->size = nr_pages << PAGE_SHIFT;
    desc->virt_addr = dma_alloc_coherent(parent, desc->size, &desc->phys_addr, GFP_KERNEL);
    if (NULL == desc->virt_addr) {
        os_free(desc);
        return NULL;
    }

    desc->parent = parent;

    return desc;
}

void os_contig_dma_free(os_contig_dma_desc_t desc)
{
    if (desc == NULL)
        return;

    if (desc->virt_addr != NULL)
        dma_free_coherent(desc->parent, desc->size, desc->virt_addr, desc->phys_addr);

    os_free(desc);
}

void *os_contig_dma_get_virt(os_contig_dma_desc_t desc)
{
    return desc->virt_addr;
}

mw_physical_addr_t os_contig_dma_get_phys(os_contig_dma_desc_t desc)
{
    return desc->phys_addr;
}

size_t os_contig_dma_get_size(os_contig_dma_desc_t desc)
{
    return desc->size;
}


/* copy */
int os_copy_in(void *kaddr, const os_user_addr_t uaddr, size_t len)
{
    if (copy_from_user(kaddr, uaddr, len) == 0)
        return OS_RETURN_SUCCESS;

    return OS_RETURN_ERROR;
}

int os_copy_out(os_user_addr_t uaddr, const void *kaddr, size_t len)
{
    if (copy_to_user(uaddr, kaddr, len) == 0)
        return OS_RETURN_SUCCESS;

    return OS_RETURN_ERROR;
}


/* internal copy */
void *os_memset(void *dst, int ch, unsigned int size)
{
    return memset(dst, ch, (size_t)size);
}

void *os_memcpy(void *dst, const void *src, unsigned int size)
{
    return memcpy(dst, src, (size_t)size);
}

void *os_memmove(void *dst, const void *src, unsigned int size)
{
    return memmove(dst, src, (size_t)size);
}

int os_memcmp(const void *s1, const void *s2, unsigned int size)
{
    return memcmp(s1, s2, (size_t)size);
}

unsigned int os_strlcpy(char *dst, const char *src, unsigned int size)
{
    return strlcpy(dst, src, (size_t)size);
}

unsigned int os_strlcat(char *dst, const char *src, unsigned int size)
{
    return strlcat(dst, src, (size_t)size);
}


/* math */
#ifndef __LP64__
long long os_div64(long long dividend, long long divisor)
{
    return div64_s64(dividend, divisor);
}
#endif


/* pci config read/write */
int os_pci_read_config_dword(os_pci_dev_t dev, int addr, unsigned int *value)
{
    if (dev == NULL)
        return OS_RETURN_INVAL;

    return pci_read_config_dword(dev, addr, value);
}

int os_pci_write_config_dword(os_pci_dev_t dev, int addr, unsigned int value)
{
    if (dev == NULL)
        return OS_RETURN_INVAL;

    return pci_write_config_dword(dev, addr, value);
}


/* something */
void os_set_freezable()
{
    set_freezable();
}

bool os_try_to_freeze()
{
    return try_to_freeze();
}


/* zlib */
#include <linux/zlib.h>
/* Returns length of decompressed data. */
int os_zlib_uncompress(uint8_t *dst, size_t *dstlen, uint8_t *src, size_t srclen)
{
    z_stream stream;
    int err;

    if (dstlen == NULL || *dstlen <= 0)
        return OS_RETURN_INVAL;

    stream.workspace = vmalloc(zlib_inflate_workspacesize());
    if ( !stream.workspace ) {
        return OS_RETURN_NOMEM;
    }
    stream.next_in = NULL;
    stream.avail_in = 0;
    zlib_inflateInit(&stream);

    stream.next_in = src;
    stream.avail_in = srclen;

    stream.next_out = dst;
    stream.avail_out = *dstlen;

    err = zlib_inflateReset(&stream);
    if (err != Z_OK) {
        printk("zlib_inflateReset error %d\n", err);
        zlib_inflateEnd(&stream);
        zlib_inflateInit(&stream);
    }

    err = zlib_inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END)
        goto err;

    zlib_inflateEnd(&stream);
    vfree(stream.workspace);

    *dstlen = stream.total_out;

    return OS_RETURN_SUCCESS;

err:
    printk("Error %d while decompressing!\n", err);
    //printk("%p(%d)->%p(%d)\n", src, srclen, dst, dstlen);
    zlib_inflateEnd(&stream);
    vfree(stream.workspace);
    return OS_RETURN_ERROR;
}

long os_args_usercopy(struct file *file, unsigned int cmd, unsigned long arg,
                      os_args_kioctl func)
{
    char    sbuf[128];
    void    *mbuf = NULL;
    void    *parg = (void *)arg;
    long    err  = -EINVAL;

    /*  Copy arguments into temp kernel buffer  */
    if (_IOC_DIR(cmd) != _IOC_NONE) {
        if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
            parg = sbuf;
        } else {
            /* too big to allocate from stack */
            mbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
            if (NULL == mbuf)
                return -ENOMEM;
            parg = mbuf;
        }

        err = -EFAULT;
        if (_IOC_DIR(cmd) & _IOC_WRITE) {
            unsigned int n = _IOC_SIZE(cmd);

            if (copy_from_user(parg, (void __user *)arg, n))
                goto out;

            /* zero out anything we don't copy from userspace */
            if (n < _IOC_SIZE(cmd))
                memset((u8 *)parg + n, 0, _IOC_SIZE(cmd) - n);
        } else {
            /* read-only ioctl */
            memset(parg, 0, _IOC_SIZE(cmd));
        }
    }

    /* Handles IOCTL */
    err = func(file, cmd, parg);
    if (err == -ENOIOCTLCMD)
        err = -ENOTTY;

    if (err < 0)
        goto out;

    /*  Copy results into user buffer  */
    switch (_IOC_DIR(cmd)) {
        case _IOC_READ:
        case (_IOC_WRITE | _IOC_READ):
            if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
                err = -EFAULT;
            break;
    }

out:
    kfree(mbuf);
    return err;
}
