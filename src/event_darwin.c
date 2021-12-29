/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
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
  OFC_EVENT_TYPE eventType ;
  OFC_BOOL signalled ;
  pthread_cond_t pthread_cond ;
  pthread_mutex_t pthread_mutex ;
} DARWIN_EVENT ;

OFC_HANDLE ofc_event_create_impl (OFC_EVENT_TYPE eventType)
{
  DARWIN_EVENT *darwin_event ;
  OFC_HANDLE hDarwinEvent ;
  pthread_cond_t pthread_cond_initializer = PTHREAD_COND_INITIALIZER ;
  pthread_mutex_t pthread_mutex_initializer = PTHREAD_MUTEX_INITIALIZER ;

  hDarwinEvent = OFC_HANDLE_NULL ;
  darwin_event = ofc_malloc (sizeof (DARWIN_EVENT)) ;
  if (darwin_event != OFC_NULL)
    {
      darwin_event->eventType = eventType ;
      darwin_event->signalled = OFC_FALSE ;
      darwin_event->pthread_cond = pthread_cond_initializer ;
      darwin_event->pthread_mutex = pthread_mutex_initializer ;
      pthread_cond_init (&darwin_event->pthread_cond, NULL)  ;
      pthread_mutex_init (&darwin_event->pthread_mutex, NULL) ;
      hDarwinEvent = ofc_handle_create (OFC_HANDLE_EVENT, darwin_event) ;
    }
  return (hDarwinEvent) ;
}

OFC_VOID ofc_event_set_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwinEvent ;
  OFC_HANDLE hWaitSet ;

  darwinEvent = ofc_handle_lock (hEvent) ;
  if (darwinEvent != OFC_NULL)
    {
      pthread_mutex_lock (&darwinEvent->pthread_mutex) ;

      darwinEvent->signalled = OFC_TRUE ;
      pthread_cond_broadcast (&darwinEvent->pthread_cond) ;

      hWaitSet = ofc_handle_get_wait_set (hEvent) ;
      ofc_handle_unlock (hEvent) ;
      if (hWaitSet != OFC_HANDLE_NULL)
	{
	  ofc_waitset_signal_impl (hWaitSet, hEvent) ;
	}

      pthread_mutex_unlock (&darwinEvent->pthread_mutex) ;
    }
}

OFC_VOID ofc_event_reset_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwinEvent ;

  darwinEvent = ofc_handle_lock (hEvent) ;
  if (darwinEvent != OFC_NULL)
    {
      pthread_mutex_lock (&darwinEvent->pthread_mutex) ;
      darwinEvent->signalled = OFC_FALSE ;
      pthread_mutex_unlock (&darwinEvent->pthread_mutex) ;
      ofc_handle_unlock (hEvent) ;
    }
}

OFC_EVENT_TYPE ofc_event_get_type_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;
  OFC_EVENT_TYPE eventType ;

  eventType = OFC_EVENT_AUTO ;
  darwin_event = ofc_handle_lock (hEvent) ;
  if (darwin_event != OFC_NULL)
    {
      eventType = darwin_event->eventType ;
      ofc_handle_unlock (hEvent) ;
    }
  return (eventType) ;
}

OFC_VOID ofc_event_destroy_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwinEvent ;

  darwinEvent = ofc_handle_lock (hEvent) ;
  if (darwinEvent != OFC_NULL)
    {
      pthread_cond_destroy (&darwinEvent->pthread_cond) ;
      pthread_mutex_destroy (&darwinEvent->pthread_mutex) ;
      ofc_free (darwinEvent) ;
      ofc_handle_destroy (hEvent) ;
      ofc_handle_unlock (hEvent) ;
    }
}

OFC_VOID ofc_event_wait_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;

  darwin_event = ofc_handle_lock (hEvent) ;
  if (darwin_event != OFC_NULL)
    {
      pthread_mutex_lock (&darwin_event->pthread_mutex) ;
      ofc_handle_unlock (hEvent) ;
      if (!darwin_event->signalled)
	pthread_cond_wait (&darwin_event->pthread_cond,
			   &darwin_event->pthread_mutex) ;
      if (darwin_event->eventType == OFC_EVENT_AUTO)
	darwin_event->signalled = OFC_FALSE ;

      pthread_mutex_unlock (&darwin_event->pthread_mutex) ;
    }
}
  
OFC_BOOL ofc_event_test_impl (OFC_HANDLE hEvent)
{
  DARWIN_EVENT *darwin_event ;
  OFC_BOOL ret ;

  ret = OFC_TRUE ;
  darwin_event = ofc_handle_lock (hEvent) ;
  if (darwin_event != OFC_NULL)
    {
      pthread_mutex_lock (&darwin_event->pthread_mutex) ;
      ret = darwin_event->signalled ;
      pthread_mutex_unlock (&darwin_event->pthread_mutex) ;
      ofc_handle_unlock (hEvent) ;
    }
  return (ret) ;
}
  
