////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////

#include "mw-event-ioctl-priv.h"
#include "ospi/ospi.h"

bool mw_event_is_event_valid_nolock(struct mw_event_io *mwev, os_event_t event)
{
    os_event_t _event = NULL;
    bool found = false;

    if (!list_empty(&mwev->event_list)) {
        list_for_each_entry(_event, &mwev->event_list, io_node) {
            if (_event == event) {
                found = true;
                break;
            }
        }
    }

    return found;
}

static long mw_ioctl_event_process(struct mw_event_io *mwev, os_iocmd_t cmd, void *arg)
{
    long ret = 0;

    switch(cmd) {
    case MW_IOCTL_KEVENT_ALLOC:
        {
            os_event_t *event = (os_event_t *)arg;

            os_spin_unlock(mwev->event_lock);
            *event = os_event_alloc();
            if (*event == NULL) {
                ret = OS_RETURN_NOMEM;
                break;
            }

            os_spin_lock(mwev->event_lock);
            list_add_tail(&(*event)->io_node, &mwev->event_list);
        }
        break;
    case MW_IOCTL_KEVENT_FREE:
        {
            os_event_t event = *(os_event_t *)arg;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            list_del_init(&event->io_node);
            os_event_free(event);
        }
        break;
    case MW_IOCTL_KEVENT_SET:
        {
            os_event_t event = *(os_event_t *)arg;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            os_event_set(event);
        }
        break;
    case MW_IOCTL_KEVENT_CLEAR:
        {
            os_event_t event = *(os_event_t *)arg;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            os_event_clear(event);
        }
        break;
    case MW_IOCTL_KEVENT_IS_SET:
        {
            os_event_t event = *(os_event_t *)arg;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            ret = os_event_is_set(event);
        }
        break;
    case MW_IOCTL_KEVENT_TRY_WAIT:
        {
            os_event_t event = *(os_event_t *)arg;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            ret = os_event_try_wait(event);
        }
        break;
    case MW_IOCTL_KEVENT_WAIT:
        {
            MW_EVENT_WAIT *ewait = (MW_EVENT_WAIT *)arg;
            os_event_t event = (os_event_t)(unsigned long)ewait->pvEvent;

            if (!mw_event_is_event_valid_nolock(mwev, event)) {
                ret = OS_RETURN_INVAL;
                break;
            }

            os_spin_unlock(mwev->event_lock);
            ret = os_event_wait(event, ewait->timeout);
            os_spin_lock(mwev->event_lock);
        }
        break;
    case MW_IOCTL_KEVENT_WAIT_MULTI:
        {
            MW_EVENT_WAIT_MULTI *ewait = (MW_EVENT_WAIT_MULTI *)arg;
            MWCAP_PTR *wrap_events;
            os_event_t *events;
            os_event_t event;
            int i;

            if (ewait->count <= 0) {
                ret = OS_RETURN_INVAL;
                break;
            }

            os_spin_unlock(mwev->event_lock); /* alloc may sleep */
            wrap_events = os_zalloc(sizeof(MWCAP_PTR) * ewait->count);
            if (wrap_events == NULL) {
                ret = OS_RETURN_NOMEM;
                os_spin_lock(mwev->event_lock);
                break;
            }

            events = os_zalloc(sizeof(os_event_t) * ewait->count);
            if (events == NULL) {
                ret = OS_RETURN_NOMEM;
                os_free(wrap_events);
                os_spin_lock(mwev->event_lock);
                break;
            }

            if (os_copy_in(wrap_events, (os_user_addr_t)(unsigned long)ewait->pvEvents, sizeof(MWCAP_PTR) * ewait->count) != 0) {
                os_free(wrap_events);
                os_free(events);
                ret = OS_RETURN_ERROR;
                os_spin_lock(mwev->event_lock);
                break;
            }

            os_spin_lock(mwev->event_lock);
            for (i = 0; i < ewait->count; i++) {
                event = (os_event_t)wrap_events[i];
                events[i] = event;

                if (!mw_event_is_event_valid_nolock(mwev, event)) {
                    break;
                }
            }
            os_free(wrap_events);
            /* have invalid */
            if (i < ewait->count) {
                ret = OS_RETURN_INVAL;
                os_free(events);
                break;
            }

            os_spin_unlock(mwev->event_lock);
            ret = os_event_wait_for_multiple(events, ewait->count, ewait->timeout);
            os_spin_lock(mwev->event_lock);

            os_free(events);
        }
        break;
    default:
        break;
    }

    return ret;
}

int mw_event_ioctl(struct mw_event_io *mwev, os_iocmd_t cmd, void *arg)
{
    long ret;

    os_spin_lock(mwev->event_lock);
    ret = mw_ioctl_event_process(mwev, cmd, arg);
    os_spin_unlock(mwev->event_lock);

    if (ret > INT_MAX)
        ret = 1;

    return (int)ret;
}

int mw_event_ioctl_init(struct mw_event_io *mwev)
{
    /* kernel event */
    mwev->event_lock = os_spin_lock_alloc();
    if (mwev->event_lock == NULL) {
        return OS_RETURN_NOMEM;
    }
    INIT_LIST_HEAD(&mwev->event_list);

    return 0;
}

void mw_event_ioctl_deinit(struct mw_event_io *mwev)
{
    os_event_t event = NULL;

    if (mwev->event_lock == NULL)
        return;

    os_spin_lock(mwev->event_lock);
    while (!list_empty(&mwev->event_list)) {
        event = list_entry(mwev->event_list.next,
                           struct _os_event_t, io_node);
        list_del(&event->io_node);

        os_event_free(event);
    }
    os_spin_unlock(mwev->event_lock);

    os_spin_lock_free(mwev->event_lock);
    mwev->event_lock = NULL;
}
