////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __OSPI_LINUX_H__
#define __OSPI_LINUX_H__

#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <linux/fs.h>

#include <linux/completion.h>

#include "mw-sg.h"

#define OS_RETURN_SUCCESS           0
#define OS_RETURN_ERROR             (-EFAULT)
#define OS_RETURN_NOMEM             (-ENOMEM)
#define OS_RETURN_INVAL             (-EINVAL)
#define OS_RETURN_NODEV             (-ENXIO)
#define OS_RETURN_BUSY              (-EBUSY)
#define OS_RETURN_NODATA            (-ENODATA)

/* init */
int os_init(void);
void os_deinit(void);


/* print */
int __os_printf(const char *s, ...);
#define os_printf(format, ...) \
    __os_printf(KERN_INFO format, ##__VA_ARGS__)

#define os_print_err(format, ...) \
    __os_printf(KERN_ERR format, ##__VA_ARGS__)

#define os_print_warning(format, ...) \
    __os_printf(KERN_WARNING format, ##__VA_ARGS__)


/* delay */
#define os_msleep(ms) msleep(ms)
#define os_udelay(us) udelay(us)


/* time */
typedef uint64_t os_clock_kick_t;

os_clock_kick_t os_get_clock_kick(void);

uint64_t os_clock_kick_to_msecs(os_clock_kick_t kick);

os_clock_kick_t os_msecs_to_clock_kick(uint64_t msecs);

/* time_after(a,b) returns true if the time a is after time b. */
int os_clock_kick_after(uint64_t a, uint64_t b);
#define os_clock_kick_before(a, b) os_clock_kick_after(b, a)

int os_clock_kick_after_eq(uint64_t a, uint64_t b);
#define os_clock_kick_before_eq(a, b) os_clock_kick_after_eq(b, a)


/* timer */
typedef struct _os_timer * os_timer;
typedef void (*timeout_func_t)(void *);
os_timer os_timer_alloc(timeout_func_t timeout_func, void *param);

void os_timer_free(os_timer timer);

void os_timer_schedule_clock_kick(os_timer timer, os_clock_kick_t kick);

void os_timer_schedule_relative(os_timer timer, uint32_t timeout_in_ms);

void os_timer_cancel(os_timer timer);


/* atomic */
typedef atomic_t os_atomic_t;

/* atomic_read - read atomic variable */
int os_atomic_read(const os_atomic_t *v);

/* atomic_set - set atomic variable */
void os_atomic_set(os_atomic_t *v, int i);

/* Atomically increments and returns the result */
void os_atomic_inc(os_atomic_t *v);

/* Atomically decrements and returns the result */
void os_atomic_dec(os_atomic_t *v);

/* Atomically adds @i to @v and returns the result */
void os_atomic_add(os_atomic_t *v, int i);

/* Atomically subtracts @i to @v and returns the result */
void os_atomic_sub(os_atomic_t *v, int i);

/* Performs an atomic OR operation and returns the original value */
void os_atomic_or(os_atomic_t *v, int i);

/* Sets @v to the specified value as an atomic operation
 * and return the original value
 */
int os_atomic_xchg(os_atomic_t *v, int i);

/* Performs an atomic compare-and-exchange
 * and return true if @newv is written to @v
 */
int os_atomic_cmpxchg(os_atomic_t *v, int oldv, int newv);

void os_clear_bit(int nr, volatile unsigned long *addr);

void os_set_bit(int nr, volatile unsigned long *addr);

int os_test_bit(int nr, const volatile unsigned long *addr);

int os_test_and_set_bit(int nr, volatile unsigned long *addr);

int os_test_and_clear_bit(int nr, volatile unsigned long *addr);


/* spin lock */
typedef spinlock_t * os_spinlock_t;
typedef unsigned long os_irq_state_t;

os_spinlock_t os_spin_lock_alloc(void);

void os_spin_lock_free(os_spinlock_t lock);

void os_spin_lock(os_spinlock_t lock);

void os_spin_unlock(os_spinlock_t lock);

void os_spin_lock_bh(os_spinlock_t lock);

void os_spin_unlock_bh(os_spinlock_t lock);

int os_spin_try_lock(os_spinlock_t lock);


/* mutex */
typedef struct mutex *os_mutex_t;

os_mutex_t os_mutex_alloc(void);

void os_mutex_free(os_mutex_t lock);

void os_mutex_lock(os_mutex_t lock);

void os_mutex_unlock(os_mutex_t lock);

int os_mutex_try_lock(os_mutex_t lock);


/* event */
#if (defined(CONFIG_PREEMPT_RT_FULL) && LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0))
#error "PREEMPT_RT_FULL not supported on kernel less than 4.4"
#endif

#ifdef RHEL_RELEASE_CODE
#if (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4))
#define OS_RHEL_8_4
#endif
#endif

struct _os_event_t {
    struct list_head        io_node;

    struct completion       done;

    // for multi wait
#if (defined(CONFIG_PREEMPT_RT_FULL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)) \
    || LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0) \
    || (defined(CONFIG_PREEMPT_RT) && LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)) \
    || defined(OS_RHEL_8_4)
    struct swait_queue      waitq;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
    wait_queue_entry_t      waitq;
#else
    wait_queue_t            waitq;
#endif
};
typedef struct _os_event_t *os_event_t;

os_event_t os_event_alloc(void);

void os_event_free(os_event_t event);

void os_event_set(os_event_t event);

void os_event_clear(os_event_t event);

bool os_event_is_set(os_event_t event);

bool os_event_try_wait(os_event_t event);

int32_t os_event_wait(os_event_t event, int32_t timeout);

/* @timeout: -1: wait for ever; 0: no wait; >0: wait for @timeout ms */
/* @ret: 0 timeout; >0 event occur */
int32_t os_event_wait_for_multiple(os_event_t events[], int num_events,
        int32_t timeout/* ms */);


/* thread*/
typedef struct _os_thread_t * os_thread_t;

os_thread_t os_thread_create_and_run(int (*threadfn)(void *data),
                                     void *data,
                                     const char *name);

void os_thread_terminate_self(os_thread_t thread);

void os_thread_wait_and_free(os_thread_t thread);


/* mem io */
uint32_t pci_read_reg32(volatile void *addr);

void pci_write_reg32(volatile void *addr, uint32_t val);


/* memory allocation */
enum {
    OS_LINUX_MEM_TYPE_KMALLOC = 0,
    OS_LINUX_MEM_TYPE_VMALLOC
};
void *os_malloc(size_t size);
void *os_zalloc(size_t size);

void os_free(void *address);

size_t os_mem_get_actual_size(void *address);
int os_mem_get_mem_type(void *address);

typedef struct _os_contig_dma_desc_t *os_contig_dma_desc_t;

typedef void * os_dma_par_t;

os_contig_dma_desc_t os_contig_dma_alloc(size_t size, os_dma_par_t par);

void os_contig_dma_free(os_contig_dma_desc_t desc);

void *os_contig_dma_get_virt(os_contig_dma_desc_t desc);

mw_physical_addr_t os_contig_dma_get_phys(os_contig_dma_desc_t desc);

size_t os_contig_dma_get_size(os_contig_dma_desc_t desc);


/* copy */
typedef void __user * os_user_addr_t;
typedef unsigned int os_iocmd_t;
int os_copy_in(void *kaddr, const os_user_addr_t uaddr, size_t len);

int os_copy_out(os_user_addr_t uaddr, const void *kaddr, size_t len);


/* internal copy */
void *os_memset(void *dst, int ch, unsigned int size);

void *os_memcpy(void *dst, const void *src, unsigned int size);

void *os_memmove(void *dst, const void *src, unsigned int size);

int os_memcmp(const void *s1, const void *s2, unsigned int size);

unsigned int os_strlcpy(char *dst, const char *src, unsigned int size);

unsigned int os_strlcat(char *dst, const char *src, unsigned int size);


/* math */
#ifdef __LP64__
#define os_div64(a, b)  ((a) / (b))
#else
long long os_div64(long long dividend, long long divisor);
#endif


/* pci config read/write */
typedef struct pci_dev * os_pci_dev_t;
int os_pci_read_config_dword(os_pci_dev_t dev, int addr, unsigned int *value);

int os_pci_write_config_dword(os_pci_dev_t dev, int addr, unsigned int value);


/* something */
void os_set_freezable(void);

bool os_try_to_freeze(void);


/* zlib */
int os_zlib_uncompress(uint8_t *dst, size_t *dstlen, uint8_t *src, size_t srclen);


typedef pid_t os_pid_t;


typedef long (*os_args_kioctl)(struct file *file, unsigned int cmd, void *arg);
long os_args_usercopy(struct file *file, unsigned int cmd, unsigned long arg,
                      os_args_kioctl func);

#endif /* __OSPI_MAC_H__ */
