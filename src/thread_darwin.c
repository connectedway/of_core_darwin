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
 * \defgroup BlueThreadDarwin Darwin Thread Interface
 * \ingroup BlueDarwin
 */

/** \{ */

typedef struct
{
  pthread_t thread ;
  OFC_DWORD (*scheduler)(BLUE_HANDLE hThread, OFC_VOID *context)  ;
  OFC_VOID *context ;
  OFC_DWORD ret ;
  OFC_BOOL deleteMe ;
  BLUE_HANDLE handle ;
  BLUE_THREAD_DETACHSTATE detachstate ;
  BLUE_HANDLE wait_set ;
  BLUE_HANDLE hNotify ;
} DARWIN_THREAD ;

static void *BlueThreadLaunch (void *arg)
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = arg ;

  darwinThread->ret = (darwinThread->scheduler)(darwinThread->handle,
						darwinThread->context) ;

  if (darwinThread->hNotify != BLUE_HANDLE_NULL)
    ofc_event_set(darwinThread->hNotify) ;

  if (darwinThread->detachstate == BLUE_THREAD_DETACH)
    {
      pthread_cancel (darwinThread->thread) ;
      BlueHandleDestroy (darwinThread->handle) ;
      BlueHeapFree (darwinThread) ;
    }
  return (OFC_NULL) ;
}

BLUE_HANDLE BlueThreadCreateImpl (OFC_DWORD(scheduler)(BLUE_HANDLE hThread,
							OFC_VOID *context),
				  OFC_CCHAR *thread_name,
				  OFC_INT thread_instance,
				  OFC_VOID *context,
				  BLUE_THREAD_DETACHSTATE detachstate,
				  BLUE_HANDLE hNotify)
{
  DARWIN_THREAD *darwinThread ;
  BLUE_HANDLE ret ;
  pthread_attr_t attr ;

  ret = BLUE_HANDLE_NULL ;
  darwinThread = BlueHeapMalloc (sizeof (DARWIN_THREAD)) ;
  if (darwinThread != OFC_NULL)
    {
      darwinThread->wait_set = BLUE_HANDLE_NULL ;
      darwinThread->deleteMe = OFC_FALSE ;
      darwinThread->scheduler = scheduler ;
      darwinThread->context = context ;
      darwinThread->hNotify = hNotify ;
      darwinThread->handle = 
	BlueHandleCreate (BLUE_HANDLE_THREAD, darwinThread) ;
      darwinThread->detachstate = detachstate ;

      pthread_attr_init (&attr) ;
      if (darwinThread->detachstate == BLUE_THREAD_DETACH)
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) ;
      else if (darwinThread->detachstate == BLUE_THREAD_JOIN)
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) ;

      if (pthread_create (&darwinThread->thread, &attr,
			  BlueThreadLaunch, darwinThread) != 0)
	{
	  BlueHandleDestroy (darwinThread->handle) ;
	  BlueHeapFree (darwinThread) ;
	}
      else
	ret = darwinThread->handle ;
    }

  return (ret) ;
}

OFC_VOID 
BlueThreadSetWaitSetImpl (BLUE_HANDLE hThread, BLUE_HANDLE wait_set) 
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != OFC_NULL)
    {
      darwinThread->wait_set = wait_set ;
      BlueHandleUnlock (hThread) ;
    }
}

OFC_VOID BlueThreadDeleteImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != OFC_NULL)
    {
      darwinThread->deleteMe = OFC_TRUE ;
      if (darwinThread->wait_set != BLUE_HANDLE_NULL)
	BlueWaitSetWake (darwinThread->wait_set) ;
      BlueHandleUnlock (hThread) ;
    }
}

OFC_VOID BlueThreadWaitImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;
  int ret ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != OFC_NULL)
    {
      if (darwinThread->detachstate == BLUE_THREAD_JOIN)
	{
	  ret = pthread_join (darwinThread->thread, OFC_NULL) ;
	  BlueHandleDestroy (darwinThread->handle) ;
	  BlueHeapFree (darwinThread) ;
	}
      BlueHandleUnlock (hThread) ;
    }
}

OFC_BOOL BlueThreadIsDeletingImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;
  OFC_BOOL ret ;

  ret = OFC_FALSE ;
  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != OFC_NULL)
    {
      if (darwinThread->deleteMe)
	ret = OFC_TRUE ;
      BlueHandleUnlock (hThread) ;
    }
  return (ret) ;
}

OFC_VOID BlueSleepImpl (OFC_DWORD milliseconds)
{
  useconds_t useconds ;

  if (milliseconds == BLUE_INFINITE)
    {
      for (;1;)
	/* Sleep for a day and keep going */
	sleep (60*60*24) ;
    }
  else
    {
      useconds = milliseconds * 1000 ;
      usleep (useconds) ;
    }
  pthread_testcancel() ;
}

OFC_DWORD BlueThreadCreateVariableImpl (OFC_VOID)
{
  pthread_key_t key ;

  pthread_key_create (&key, NULL) ;
  return ((OFC_DWORD) key) ;
}

OFC_VOID BlueThreadDestroyVariableImpl (OFC_DWORD dkey)
{
  pthread_key_t key ;
  key = (pthread_key_t) dkey ;

  pthread_key_delete (key);
}

OFC_DWORD_PTR BlueThreadGetVariableImpl (OFC_DWORD var) 
{
  return ((OFC_DWORD_PTR) pthread_getspecific ((pthread_key_t) var)) ;
}

OFC_VOID BlueThreadSetVariableImpl (OFC_DWORD var, OFC_DWORD_PTR val) 
{
  pthread_setspecific ((pthread_key_t) var, (const void *) val) ;
}  

/*
 * These routines are noops on platforms that support TLS
 */
OFC_CORE_LIB OFC_VOID
BlueThreadCreateLocalStorageImpl (OFC_VOID) 
{
}

OFC_CORE_LIB OFC_VOID
BlueThreadDestroyLocalStorageImpl (OFC_VOID)
{
}

OFC_CORE_LIB OFC_VOID
BlueThreadInitImpl (OFC_VOID)
{
}

OFC_CORE_LIB OFC_VOID
BlueThreadDestroyImpl (OFC_VOID)
{
}

/** \} */
