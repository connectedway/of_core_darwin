/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "ofc/core.h"
#include "ofc/types.h"
#include "ofc/handle.h"
#include "ofc/thread.h"
#include "ofc/impl/threadimpl.h"
#include "ofc/sched.h"
#include "ofc/waitset.h"
#include "ofc/event.h"

#include "ofc/heap.h"

/**
 * \defgroup thread_darwin Darwin Thread Interface
 * \ingroup darwin
 */

/** \{ */

typedef struct {
    pthread_t thread;

    OFC_DWORD (*scheduler)(OFC_HANDLE hThread, OFC_VOID *context);

    OFC_VOID *context;
    OFC_DWORD ret;
    OFC_BOOL deleteMe;
    OFC_HANDLE handle;
    OFC_THREAD_DETACHSTATE detachstate;
    OFC_HANDLE wait_set;
    OFC_HANDLE hNotify;
} DARWIN_THREAD;

static void *ofc_thread_launch(void *arg) {
    DARWIN_THREAD *darwinThread;

    darwinThread = arg;

    darwinThread->ret = (darwinThread->scheduler)(darwinThread->handle,
                                                  darwinThread->context);

    if (darwinThread->hNotify != OFC_HANDLE_NULL)
        ofc_event_set(darwinThread->hNotify);

    if (darwinThread->detachstate == OFC_THREAD_DETACH) {
        pthread_cancel(darwinThread->thread);
        ofc_handle_destroy(darwinThread->handle);
        ofc_free(darwinThread);
    }
    return (OFC_NULL);
}

OFC_HANDLE ofc_thread_create_impl(OFC_DWORD(scheduler)(OFC_HANDLE hThread,
                                                       OFC_VOID *context),
                                  OFC_CCHAR *thread_name,
                                  OFC_INT thread_instance,
                                  OFC_VOID *context,
                                  OFC_THREAD_DETACHSTATE detachstate,
                                  OFC_HANDLE hNotify) {
    DARWIN_THREAD *darwinThread;
    OFC_HANDLE ret;
    pthread_attr_t attr;

    ret = OFC_HANDLE_NULL;
    darwinThread = ofc_malloc(sizeof(DARWIN_THREAD));
    if (darwinThread != OFC_NULL) {
        darwinThread->wait_set = OFC_HANDLE_NULL;
        darwinThread->deleteMe = OFC_FALSE;
        darwinThread->scheduler = scheduler;
        darwinThread->context = context;
        darwinThread->hNotify = hNotify;
        darwinThread->handle =
                ofc_handle_create(OFC_HANDLE_THREAD, darwinThread);
        darwinThread->detachstate = detachstate;

        pthread_attr_init(&attr);
        if (darwinThread->detachstate == OFC_THREAD_DETACH)
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        else if (darwinThread->detachstate == OFC_THREAD_JOIN)
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        if (pthread_create(&darwinThread->thread, &attr,
                           ofc_thread_launch, darwinThread) != 0) {
            ofc_handle_destroy(darwinThread->handle);
            ofc_free(darwinThread);
        } else
            ret = darwinThread->handle;
    }

    return (ret);
}

OFC_VOID
ofc_thread_set_waitset_impl(OFC_HANDLE hThread, OFC_HANDLE wait_set) {
    DARWIN_THREAD *darwinThread;

    darwinThread = ofc_handle_lock(hThread);
    if (darwinThread != OFC_NULL) {
        darwinThread->wait_set = wait_set;
        ofc_handle_unlock(hThread);
    }
}

OFC_VOID ofc_thread_delete_impl(OFC_HANDLE hThread) {
    DARWIN_THREAD *darwinThread;

    darwinThread = ofc_handle_lock(hThread);
    if (darwinThread != OFC_NULL) {
        darwinThread->deleteMe = OFC_TRUE;
        if (darwinThread->wait_set != OFC_HANDLE_NULL)
            ofc_waitset_wake(darwinThread->wait_set);
        ofc_handle_unlock(hThread);
    }
}

OFC_VOID ofc_thread_wait_impl(OFC_HANDLE hThread) {
    DARWIN_THREAD *darwinThread;
    int ret;

    darwinThread = ofc_handle_lock(hThread);
    if (darwinThread != OFC_NULL) {
        if (darwinThread->detachstate == OFC_THREAD_JOIN) {
            ret = pthread_join(darwinThread->thread, OFC_NULL);
            ofc_handle_destroy(darwinThread->handle);
            ofc_free(darwinThread);
        }
        ofc_handle_unlock(hThread);
    }
}

OFC_BOOL ofc_thread_is_deleting_impl(OFC_HANDLE hThread) {
    DARWIN_THREAD *darwinThread;
    OFC_BOOL ret;

    ret = OFC_FALSE;
    darwinThread = ofc_handle_lock(hThread);
    if (darwinThread != OFC_NULL) {
        if (darwinThread->deleteMe)
            ret = OFC_TRUE;
        ofc_handle_unlock(hThread);
    }
    return (ret);
}

OFC_VOID ofc_sleep_impl(OFC_DWORD milliseconds) {
    useconds_t useconds;

    if (milliseconds == OFC_INFINITE) {
        for (; 1;)
            /* Sleep for a day and keep going */
            sleep(60 * 60 * 24);
    } else {
        useconds = milliseconds * 1000;
        usleep(useconds);
    }
    pthread_testcancel();
}

OFC_DWORD ofc_thread_create_variable_impl(OFC_VOID) {
    pthread_key_t key;

    pthread_key_create(&key, NULL);
    return ((OFC_DWORD) key);
}

OFC_VOID ofc_thread_destroy_variable_impl(OFC_DWORD dkey) {
    pthread_key_t key;
    key = (pthread_key_t) dkey;

    pthread_key_delete(key);
}

OFC_DWORD_PTR ofc_thread_get_variable_impl(OFC_DWORD var) {
    return ((OFC_DWORD_PTR) pthread_getspecific((pthread_key_t) var));
}

OFC_VOID ofc_thread_set_variable_impl(OFC_DWORD var, OFC_DWORD_PTR val) {
    pthread_setspecific((pthread_key_t) var, (const void *) val);
}

/*
 * These routines are noops on platforms that support TLS
 */
OFC_CORE_LIB OFC_VOID
ofc_thread_create_local_storage_impl(OFC_VOID) {
}

OFC_CORE_LIB OFC_VOID
ofc_thread_destroy_local_storage_impl(OFC_VOID) {
}

OFC_CORE_LIB OFC_VOID
ofc_thread_init_impl(OFC_VOID) {
}

OFC_CORE_LIB OFC_VOID
ofc_thread_destroy_impl(OFC_VOID) {
}

OFC_CORE_LIB OFC_VOID
ofc_thread_detach_impl(OFC_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = ofc_handle_lock (hThread) ;
  if (darwinThread != OFC_NULL)
    {
      darwinThread->detachstate = OFC_THREAD_DETACH;
      pthread_detach(darwinThread->thread);
      ofc_handle_unlock(hThread) ;
    }
}
/** \} */
