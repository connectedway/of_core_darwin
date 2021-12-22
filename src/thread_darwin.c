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
  BLUE_DWORD (*scheduler)(BLUE_HANDLE hThread, BLUE_VOID *context)  ;
  BLUE_VOID *context ;
  BLUE_DWORD ret ;
  BLUE_BOOL deleteMe ;
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
    BlueEventSet (darwinThread->hNotify) ;

  if (darwinThread->detachstate == BLUE_THREAD_DETACH)
    {
      pthread_cancel (darwinThread->thread) ;
      BlueHandleDestroy (darwinThread->handle) ;
      BlueHeapFree (darwinThread) ;
    }
  return (BLUE_NULL) ;
}

BLUE_HANDLE BlueThreadCreateImpl (BLUE_DWORD(scheduler)(BLUE_HANDLE hThread,
							BLUE_VOID *context),
				  BLUE_CCHAR *thread_name,
				  BLUE_INT thread_instance,
				  BLUE_VOID *context,
				  BLUE_THREAD_DETACHSTATE detachstate,
				  BLUE_HANDLE hNotify)
{
  DARWIN_THREAD *darwinThread ;
  BLUE_HANDLE ret ;
  pthread_attr_t attr ;

  ret = BLUE_HANDLE_NULL ;
  darwinThread = BlueHeapMalloc (sizeof (DARWIN_THREAD)) ;
  if (darwinThread != BLUE_NULL)
    {
      darwinThread->wait_set = BLUE_HANDLE_NULL ;
      darwinThread->deleteMe = BLUE_FALSE ;
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

BLUE_VOID 
BlueThreadSetWaitSetImpl (BLUE_HANDLE hThread, BLUE_HANDLE wait_set) 
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != BLUE_NULL)
    {
      darwinThread->wait_set = wait_set ;
      BlueHandleUnlock (hThread) ;
    }
}

BLUE_VOID BlueThreadDeleteImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != BLUE_NULL)
    {
      darwinThread->deleteMe = BLUE_TRUE ;
      if (darwinThread->wait_set != BLUE_HANDLE_NULL)
	BlueWaitSetWake (darwinThread->wait_set) ;
      BlueHandleUnlock (hThread) ;
    }
}

BLUE_VOID BlueThreadWaitImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;
  int ret ;

  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != BLUE_NULL)
    {
      if (darwinThread->detachstate == BLUE_THREAD_JOIN)
	{
	  ret = pthread_join (darwinThread->thread, BLUE_NULL) ;
	  BlueHandleDestroy (darwinThread->handle) ;
	  BlueHeapFree (darwinThread) ;
	}
      BlueHandleUnlock (hThread) ;
    }
}

BLUE_BOOL BlueThreadIsDeletingImpl (BLUE_HANDLE hThread)
{
  DARWIN_THREAD *darwinThread ;
  BLUE_BOOL ret ;

  ret = BLUE_FALSE ;
  darwinThread = BlueHandleLock (hThread) ;
  if (darwinThread != BLUE_NULL)
    {
      if (darwinThread->deleteMe)
	ret = BLUE_TRUE ;
      BlueHandleUnlock (hThread) ;
    }
  return (ret) ;
}

BLUE_VOID BlueSleepImpl (BLUE_DWORD milliseconds)
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

BLUE_DWORD BlueThreadCreateVariableImpl (BLUE_VOID)
{
  pthread_key_t key ;

  pthread_key_create (&key, NULL) ;
  return ((BLUE_DWORD) key) ;
}

BLUE_VOID BlueThreadDestroyVariableImpl (BLUE_DWORD dkey)
{
  pthread_key_t key ;
  key = (pthread_key_t) dkey ;

  pthread_key_delete (key);
}

BLUE_DWORD_PTR BlueThreadGetVariableImpl (BLUE_DWORD var) 
{
  return ((BLUE_DWORD_PTR) pthread_getspecific ((pthread_key_t) var)) ;
}

BLUE_VOID BlueThreadSetVariableImpl (BLUE_DWORD var, BLUE_DWORD_PTR val) 
{
  pthread_setspecific ((pthread_key_t) var, (const void *) val) ;
}  

/*
 * These routines are noops on platforms that support TLS
 */
BLUE_CORE_LIB BLUE_VOID
BlueThreadCreateLocalStorageImpl (BLUE_VOID) 
{
}

BLUE_CORE_LIB BLUE_VOID
BlueThreadDestroyLocalStorageImpl (BLUE_VOID)
{
}

BLUE_CORE_LIB BLUE_VOID
BlueThreadInitImpl (BLUE_VOID)
{
}

BLUE_CORE_LIB BLUE_VOID
BlueThreadDestroyImpl (BLUE_VOID)
{
}

/** \} */
