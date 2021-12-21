/* Copyright (c) 2009 Blue Peach Solutions, Inc.
 * All rights reserved.
 *
 * This software is protected by copyright and intellectual 
 * property laws as well as international treaties.  It is to be 
 * used and copied only by authorized licensees under the 
 * conditions described in their licenses.  
 *
 * Title to and ownership of the software shall at all times 
 * remain with Blue Peach Solutions.
 */
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "ofc/types.h"
#include "ofc/handle.h"
#include "ofc/event.h"

#include "ofc/heap.h"
#include "ofc/impl/eventimpl.h"
#include "ofc/impl/waitsetimpl.h"

typedef struct
{
  BLUE_EVENT_TYPE eventType ;
  BLUE_BOOL signalled ;
  pthread_cond_t pthread_cond ;
  pthread_mutex_t pthread_mutex ;
} DARWIN_EVENT ;

BLUE_HANDLE BlueEventCreateImpl (BLUE_EVENT_TYPE eventType)
{
  DARWIN_EVENT *darwin_event ;
  BLUE_HANDLE hDarwinEvent ;
  pthread_cond_t pthread_cond_initializer = PTHREAD_COND_INITIALIZER ;
  pthread_mutex_t pthread_mutex_initializer = PTHREAD_MUTEX_INITIALIZER ;

  hDarwinEvent = BLUE_HANDLE_NULL ;
  darwin_event = BlueHeapMalloc (sizeof (DARWIN_EVENT)) ;
  if (darwin_event != BLUE_NULL)
    {
      darwin_event->eventType = eventType ;
      darwin_event->signalled = BLUE_FALSE ;
      darwin_event->pthread_cond = pthread_cond_initializer ;
      darwin_event->pthread_mutex = pthread_mutex_initializer ;
      pthread_cond_init (&darwin_event->pthread_cond, NULL)  ;
      pthread_mutex_init (&darwin_event->pthread_mutex, NULL) ;
      hDarwinEvent = BlueHandleCreate (BLUE_HANDLE_EVENT, darwin_event) ;
    }
  return (hDarwinEvent) ;
}

BLUE_VOID BlueEventSetImpl (BLUE_HANDLE hEvent)
{
  DARWIN_EVENT *darwinEvent ;
  BLUE_HANDLE hWaitSet ;

  darwinEvent = BlueHandleLock (hEvent) ;
  if (darwinEvent != BLUE_NULL)
    {
      pthread_mutex_lock (&darwinEvent->pthread_mutex) ;

      darwinEvent->signalled = BLUE_TRUE ;
      pthread_cond_broadcast (&darwinEvent->pthread_cond) ;

      hWaitSet = BlueHandleGetWaitSet (hEvent) ;
      BlueHandleUnlock (hEvent) ;
      if (hWaitSet != BLUE_HANDLE_NULL)
	{
	  BlueWaitSetSignalImpl (hWaitSet, hEvent) ;
	}

      pthread_mutex_unlock (&darwinEvent->pthread_mutex) ;
    }
}

BLUE_VOID BlueEventResetImpl (BLUE_HANDLE hEvent)
{
  DARWIN_EVENT *darwinEvent ;

  darwinEvent = BlueHandleLock (hEvent) ;
  if (darwinEvent != BLUE_NULL)
    {
      pthread_mutex_lock (&darwinEvent->pthread_mutex) ;
      darwinEvent->signalled = BLUE_FALSE ;
      pthread_mutex_unlock (&darwinEvent->pthread_mutex) ;
      BlueHandleUnlock (hEvent) ;
    }
}

BLUE_EVENT_TYPE BlueEventGetTypeImpl (BLUE_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;
  BLUE_EVENT_TYPE eventType ;

  eventType = BLUE_EVENT_AUTO ;
  darwin_event = BlueHandleLock (hEvent) ;
  if (darwin_event != BLUE_NULL)
    {
      eventType = darwin_event->eventType ;
      BlueHandleUnlock (hEvent) ;
    }
  return (eventType) ;
}

BLUE_VOID BlueEventDestroyImpl (BLUE_HANDLE hEvent)  
{
  DARWIN_EVENT *darwinEvent ;

  darwinEvent = BlueHandleLock (hEvent) ;
  if (darwinEvent != BLUE_NULL)
    {
      pthread_cond_destroy (&darwinEvent->pthread_cond) ;
      pthread_mutex_destroy (&darwinEvent->pthread_mutex) ;
      BlueHeapFree (darwinEvent) ;
      BlueHandleDestroy (hEvent) ;
      BlueHandleUnlock (hEvent) ;
    }
}

BLUE_VOID BlueEventWaitImpl (BLUE_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;

  darwin_event = BlueHandleLock (hEvent) ;
  if (darwin_event != BLUE_NULL)
    {
      pthread_mutex_lock (&darwin_event->pthread_mutex) ;
      BlueHandleUnlock (hEvent) ;
      if (!darwin_event->signalled)
	pthread_cond_wait (&darwin_event->pthread_cond,
			   &darwin_event->pthread_mutex) ;
      if (darwin_event->eventType == BLUE_EVENT_AUTO)
	darwin_event->signalled = BLUE_FALSE ;

      pthread_mutex_unlock (&darwin_event->pthread_mutex) ;
    }
}
  
BLUE_BOOL BlueEventTestImpl (BLUE_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;
  BLUE_BOOL ret ;

  ret = BLUE_TRUE ;
  darwin_event = BlueHandleLock (hEvent) ;
  if (darwin_event != BLUE_NULL)
    {
      pthread_mutex_lock (&darwin_event->pthread_mutex) ;
      ret = darwin_event->signalled ;
      pthread_mutex_unlock (&darwin_event->pthread_mutex) ;
      BlueHandleUnlock (hEvent) ;
    }
  return (ret) ;
}
  
