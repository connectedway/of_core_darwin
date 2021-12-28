/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include "ofc/config.h"
#include "ofc/types.h"
#include "ofc/handle.h"
#include "ofc/waitq.h"
#include "ofc/timer.h"
#include "ofc/queue.h"
#include "ofc/socket.h"
#include "ofc/event.h"
#include "ofc/waitset.h"
#include "ofc/impl/waitsetimpl.h"
#include "ofc/impl/socketimpl.h"
#include "ofc/impl/eventimpl.h"
#include "ofc/libc.h"
#if defined(OFC_APP_DEBUG)
#include "ofc/app.h"
#endif

#include "ofc/heap.h"

#include "ofc/fs.h"
#include "ofc/file.h"

#include "ofc_darwin/fs_darwin.h"

/**
 * \defgroup BlueWaitSetWin32 Win32 Dependent Scheduler Handling
 * \ingroup BlueWin32
 */

/** \{ */

typedef struct
{
  int pipe_files[2] ;
} DARWIN_WAIT_SET ;

OFC_VOID BlueWaitSetCreateImpl (WAIT_SET *pWaitSet) 
{
  DARWIN_WAIT_SET *DarwinWaitSet ;

  DarwinWaitSet = ofc_malloc (sizeof (DARWIN_WAIT_SET)) ;
  pWaitSet->impl = DarwinWaitSet ;
  pipe (DarwinWaitSet->pipe_files) ;
  fcntl (DarwinWaitSet->pipe_files[0], F_SETFL,
	 fcntl (DarwinWaitSet->pipe_files[0], F_GETFL) | O_NONBLOCK) ;
  fcntl (DarwinWaitSet->pipe_files[1], F_SETFL,
	 fcntl (DarwinWaitSet->pipe_files[1], F_GETFL) | O_NONBLOCK) ;
}

OFC_VOID BlueWaitSetDestroyImpl (WAIT_SET *pWaitSet) 
{
  DARWIN_WAIT_SET *DarwinWaitSet ;

  DarwinWaitSet = pWaitSet->impl ;
  close (DarwinWaitSet->pipe_files[0]) ;
  close (DarwinWaitSet->pipe_files[1]) ;
  ofc_free (pWaitSet->impl) ;
  pWaitSet->impl = OFC_NULL ;
}

typedef struct
{
  OFC_HANDLE hEvent ;
  OFC_HANDLE hAssoc ;
} EVENT_ELEMENT ;

OFC_VOID BlueWaitSetSignalImpl (OFC_HANDLE handle, OFC_HANDLE hEvent)
{
  WAIT_SET *pWaitSet ;
  DARWIN_WAIT_SET *DarwinWaitSet ;

  pWaitSet = ofc_handle_lock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      DarwinWaitSet = pWaitSet->impl ;
      ofc_handle_unlock (handle) ;
      write (DarwinWaitSet->pipe_files[1], &hEvent, sizeof (OFC_HANDLE)) ;
    }
}

OFC_VOID BlueWaitSetWakeImpl (OFC_HANDLE handle)
{
  BlueWaitSetSignalImpl (handle, OFC_HANDLE_NULL) ;
}

OFC_HANDLE PollEvent (OFC_INT fd, OFC_HANDLE eventQueue)
{
  EVENT_ELEMENT *eventElement ;
  OFC_HANDLE hEvent ;
  OFC_HANDLE triggered_event ;
  OFC_SIZET size ;
  OFC_BOOL wake ;
  /*
   * Special case.  It's the pipe.  Let's read the
   * event handle
   */
  triggered_event = OFC_HANDLE_NULL ;
  wake = OFC_FALSE ;

  do
    {
      size = read (fd, &hEvent, sizeof(OFC_HANDLE)) ;
      if (size == sizeof (OFC_HANDLE))
	{
	  if (hEvent == OFC_HANDLE_NULL)
	    wake = OFC_TRUE ;
	  else
	    {
	      for (eventElement = BlueQfirst (eventQueue) ;
		   eventElement != OFC_NULL && eventElement->hEvent != hEvent ;
		   eventElement = BlueQnext (eventQueue, eventElement) ) ;

	      if (eventElement != OFC_NULL)
		{
		  if (ofc_event_test (hEvent) == OFC_TRUE)
		    {
		      if (ofc_event_get_type(hEvent) == OFC_EVENT_AUTO)
			ofc_event_reset (hEvent) ;
		      triggered_event = eventElement->hAssoc ;
		    }
		}
	    }
	}
    }
  while (triggered_event == OFC_HANDLE_NULL && !wake &&
	 size == sizeof (OFC_HANDLE)) ;

  return (triggered_event);
}

OFC_HANDLE BlueWaitSetWaitImpl (OFC_HANDLE handle)
{
  WAIT_SET *pWaitSet ;
  DARWIN_WAIT_SET *DarwinWaitSet ;

  OFC_HANDLE hEvent ;
  OFC_HANDLE hEventHandle ;
  OFC_HANDLE triggered_event ;
  OFC_HANDLE timer_event ;
  OFC_HANDLE darwinHandle ;
#if defined(OFC_FS_DARWIN)
  OFC_HANDLE fsHandle ;
  OFC_FST_TYPE fsType ;
#endif
  struct pollfd *darwin_handle_list ;
  OFC_HANDLE *blue_handle_list ;

  nfds_t wait_count ;
  int wait_index ;
  int leastWait ;

  int poll_count ;
  OFC_MSTIME wait_time ;
  OFC_HANDLE eventQueue ;
  EVENT_ELEMENT *eventElement ;
  OFC_HANDLE hWaitQ ;

  triggered_event = OFC_HANDLE_NULL ;
  pWaitSet = ofc_handle_lock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      eventQueue = BlueQcreate () ;
      leastWait = OFC_MAX_SCHED_WAIT ;
      timer_event = OFC_HANDLE_NULL ;

      wait_count = 0 ;
      darwin_handle_list = ofc_malloc (sizeof (struct pollfd)) ;
      blue_handle_list = ofc_malloc (sizeof (OFC_HANDLE)) ;

      DarwinWaitSet = pWaitSet->impl ;

      /*
       * Purge any additional queued events.  We'll get these before we
       * sleep the next time
       */
      while (read (DarwinWaitSet->pipe_files[0], &hEventHandle,
		   sizeof (OFC_HANDLE)) > 0) ;
      darwin_handle_list[wait_count].fd = DarwinWaitSet->pipe_files[0] ;
      darwin_handle_list[wait_count].events = POLLIN ;
      darwin_handle_list[wait_count].revents = 0 ;
      blue_handle_list[wait_count] = OFC_HANDLE_NULL ;

      wait_count++ ;

      for (hEventHandle = 
	     (OFC_HANDLE) BlueQfirst (pWaitSet->hHandleQueue) ;
		   hEventHandle != OFC_HANDLE_NULL &&
		   triggered_event == OFC_HANDLE_NULL ;
	   hEventHandle = 
	     (OFC_HANDLE) BlueQnext (pWaitSet->hHandleQueue,
                                 (OFC_VOID *) hEventHandle) )
	{
	  switch (ofc_handle_get_type(hEventHandle))
	    {
	    default:
	    case OFC_HANDLE_WAIT_SET:
	    case OFC_HANDLE_SCHED:
	    case OFC_HANDLE_APP:
	    case OFC_HANDLE_THREAD:
	    case OFC_HANDLE_PIPE:
	    case OFC_HANDLE_MAILSLOT:
	    case OFC_HANDLE_FSWIN32_FILE:
	    case OFC_HANDLE_FSDARWIN_FILE:
	    case OFC_HANDLE_QUEUE:
	      /*
	       * These are not synchronizeable.  Simple ignore
	       */
	      break ;

	    case OFC_HANDLE_WAIT_QUEUE:
	      hEvent = BlueWaitQGetEventHandle (hEventHandle) ;
	      if (!BlueWaitQempty (hEventHandle))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = ofc_malloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case OFC_HANDLE_FILE:
#if defined(OFC_FS_DARWIN)
	      fsType = OfcFileGetFSType(hEventHandle) ;

	      if (fsType == OFC_FST_DARWIN)
		{
		  darwin_handle_list = 
		    ofc_realloc (darwin_handle_list,
				     sizeof (struct pollfd) * (wait_count+1)) ;
		  blue_handle_list = 
		    ofc_realloc (blue_handle_list,
                             sizeof (OFC_HANDLE) * (wait_count + 1)) ;
		  fsHandle = OfcFileGetFSHandle (hEventHandle) ;
		  darwin_handle_list[wait_count].fd = 
		    OfcFSDarwinGetFD (fsHandle) ;
		  darwin_handle_list[wait_count].events = 0 ;
		  darwin_handle_list[wait_count].revents = 0 ;
		  blue_handle_list[wait_count] = hEventHandle ;
		  wait_count++ ;
		}
#endif
	      break ;
	    case OFC_HANDLE_SOCKET:
	      /*
	       * Wait on event
	       */
	      darwin_handle_list = 
		ofc_realloc (darwin_handle_list,
				 sizeof (struct pollfd) * (wait_count+1)) ;
	      blue_handle_list = 
		ofc_realloc (blue_handle_list,
                         sizeof (OFC_HANDLE) * (wait_count + 1)) ;
	      
	      darwinHandle = BlueSocketGetImpl (hEventHandle) ;
	      darwin_handle_list[wait_count].fd = 
		BlueSocketImplGetFD (darwinHandle) ;
	      darwin_handle_list[wait_count].events = 
		BlueSocketImplGetEvent (darwinHandle) ;
	      darwin_handle_list[wait_count].revents = 0 ;
	      blue_handle_list[wait_count] = hEventHandle ;
	      wait_count++ ;
	      break ;

	    case OFC_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
	      hEvent = OfcFSDarwinGetOverlappedEvent (hEventHandle) ;
	      if (ofc_event_test (hEvent))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = ofc_malloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
#endif
	      break ;

	    case OFC_HANDLE_FSSMB_OVERLAPPED:
	      hWaitQ = OfcFileGetOverlappedWaitQ (hEventHandle) ;
	      hEvent = BlueWaitQGetEventHandle (hWaitQ) ;
	      if (!BlueWaitQempty (hWaitQ))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = ofc_malloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case OFC_HANDLE_EVENT:
	      if (ofc_event_test (hEventHandle))
		{
		  triggered_event = hEventHandle ;
		  if (ofc_event_get_type (hEventHandle) == OFC_EVENT_AUTO)
		    ofc_event_reset (hEventHandle) ;
		}
	      else
		{
		  eventElement = ofc_malloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEventHandle ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case OFC_HANDLE_TIMER:
	      wait_time = BlueTimerGetWaitTime (hEventHandle) ;
	      if (wait_time == 0)
		triggered_event = hEventHandle ;
	      else
		{
		  if (wait_time < leastWait)
		    {
		      leastWait = wait_time ;
		      timer_event = hEventHandle ;
		    }
		}
	      break ;

	    }
	}

      ofc_handle_unlock (handle) ;

      if (triggered_event == OFC_HANDLE_NULL)
	{
	  poll_count = poll (darwin_handle_list, wait_count, leastWait) ;
	  if (poll_count == 0 && timer_event != OFC_HANDLE_NULL)
	    triggered_event = timer_event ;
	  else if (poll_count > 0)
	    {
	      for (wait_index = 0 ; 
		   (wait_index < wait_count && 
		    darwin_handle_list[wait_index].revents == 0) ; 
		   wait_index++) ;

	      if (wait_index == 0)
		triggered_event = 
		  PollEvent(DarwinWaitSet->pipe_files[0], eventQueue) ;
	      else if (wait_index < wait_count)
		{
		  BlueSocketImplSetEvent 
		    (BlueSocketGetImpl (blue_handle_list[wait_index]),
		     darwin_handle_list[wait_index].revents) ;
		      
		  if (darwin_handle_list[wait_index].revents != 0)
		    triggered_event = blue_handle_list[wait_index] ;
		}
	    }
	}

      for (eventElement = BlueQdequeue (eventQueue) ;
	   eventElement != OFC_NULL ;
	   eventElement = BlueQdequeue (eventQueue))
	ofc_free (eventElement) ;

      BlueQdestroy (eventQueue) ;

      ofc_free (darwin_handle_list) ;
      ofc_free (blue_handle_list) ;

    }
  return (triggered_event) ;
}

OFC_VOID BlueWaitSetSetAssocImpl (OFC_HANDLE hEvent,
                                  OFC_HANDLE hApp, OFC_HANDLE hSet)
{
  OFC_HANDLE hAssoc ;

  switch (ofc_handle_get_type(hEvent))
    {
    default:
    case OFC_HANDLE_WAIT_SET:
    case OFC_HANDLE_SCHED:
    case OFC_HANDLE_APP:
    case OFC_HANDLE_THREAD:
    case OFC_HANDLE_PIPE:
    case OFC_HANDLE_MAILSLOT:
    case OFC_HANDLE_FSWIN32_FILE:
    case OFC_HANDLE_QUEUE:
      /*
       * These are not synchronizeable.  Simple ignore
       */
      break ;

    case OFC_HANDLE_WAIT_QUEUE:
      hAssoc = BlueWaitQGetEventHandle (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
      break ;

    case OFC_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
      hAssoc = OfcFSDarwinGetOverlappedEvent (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
#endif
      break ;

    case OFC_HANDLE_FSSMB_OVERLAPPED:
      hAssoc = OfcFileGetOverlappedEvent (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
      break ;

    case OFC_HANDLE_EVENT:
    case OFC_HANDLE_FILE:
    case OFC_HANDLE_SOCKET:
    case OFC_HANDLE_TIMER:
      /*
       * These don't need to set associated events
       */
      break ;
    }
}
OFC_VOID BlueWaitSetAddImpl (OFC_HANDLE hSet, OFC_HANDLE hApp,
                             OFC_HANDLE hEvent)
{
  OFC_HANDLE hAssoc ;

  switch (ofc_handle_get_type(hEvent))
    {
    default:
    case OFC_HANDLE_WAIT_SET:
    case OFC_HANDLE_SCHED:
    case OFC_HANDLE_APP:
    case OFC_HANDLE_THREAD:
    case OFC_HANDLE_PIPE:
    case OFC_HANDLE_MAILSLOT:
    case OFC_HANDLE_FSWIN32_FILE:
    case OFC_HANDLE_QUEUE:
      /*
       * These are not synchronizeable.  Simple ignore
       */
      break ;

    case OFC_HANDLE_WAIT_QUEUE:
      hAssoc = BlueWaitQGetEventHandle (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
      if (!BlueWaitQempty (hEvent))
	BlueWaitSetSignalImpl (hSet, hAssoc) ;
      break ;

    case OFC_HANDLE_EVENT:
      ofc_handle_set_app (hEvent, hApp, hSet) ;
      if (ofc_event_test (hEvent))
	BlueWaitSetSignalImpl (hSet, hEvent) ;
      break ;

    case OFC_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
      hAssoc = OfcFSDarwinGetOverlappedEvent (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
      if (ofc_event_test (hAssoc))
	BlueWaitSetSignalImpl (hSet, hAssoc) ;
#endif
      break ;

    case OFC_HANDLE_FSSMB_OVERLAPPED:
      hAssoc = OfcFileGetOverlappedEvent (hEvent) ;
      ofc_handle_set_app (hAssoc, hApp, hSet) ;
      if (ofc_event_test (hAssoc))
	{
	  BlueWaitSetSignalImpl (hSet, hAssoc) ;
	}
      break ;

    case OFC_HANDLE_FILE:
    case OFC_HANDLE_SOCKET:
    case OFC_HANDLE_TIMER:
      /*
       * These don't need to set associated events
       */
      break ;
    }
}

OFC_VOID BlueWaitSetDebug (OFC_HANDLE handle)
{
  WAIT_SET *pWaitSet ;

  OFC_HANDLE hEvent ;
  OFC_HANDLE hEventHandle ;
  OFC_HANDLE hWaitQ ;
#if defined(OFC_FS_DARWIN)
  OFC_FST_TYPE fsType ;
#endif

  pWaitSet = ofc_handle_lock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      for (hEventHandle = 
	     (OFC_HANDLE) BlueQfirst (pWaitSet->hHandleQueue) ;
		   hEventHandle != OFC_HANDLE_NULL ;
	   hEventHandle = 
	     (OFC_HANDLE) BlueQnext (pWaitSet->hHandleQueue,
                                 (OFC_VOID *) hEventHandle) )
	{
	  switch (ofc_handle_get_type(hEventHandle))
	    {
	    default:
	    case OFC_HANDLE_WAIT_SET:
	    case OFC_HANDLE_SCHED:
	    case OFC_HANDLE_APP:
	    case OFC_HANDLE_THREAD:
	    case OFC_HANDLE_PIPE:
	    case OFC_HANDLE_MAILSLOT:
	    case OFC_HANDLE_FSWIN32_FILE:
	    case OFC_HANDLE_FSDARWIN_FILE:
	    case OFC_HANDLE_QUEUE:
	      /*
	       * These are not synchronizeable.  Simple ignore
	       */
	      break ;

	    case OFC_HANDLE_WAIT_QUEUE:
	      ofc_printf ("Wait Queue: %s\n",
			   BlueWaitQempty(hEventHandle) ? 
			   "empty" : "not empty") ;
	      break ;

	    case OFC_HANDLE_FILE:
#if defined(OFC_FS_DARWIN)
	      fsType = OfcFileGetFSType(hEventHandle) ;
	      if (fsType == OFC_FST_DARWIN)
		{
		  ofc_printf ("Darwin File\n") ;
		}
#endif
	      break ;

	    case OFC_HANDLE_SOCKET:
	      ofc_printf ("Darwin Socket\n") ;
	      break ;

	    case OFC_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
	      hEvent = OfcFSDarwinGetOverlappedEvent (hEventHandle) ;
	      ofc_printf ("Darwin Overlapped: %s\n",
			   ofc_event_test (hEvent) ? 
			   "triggered" : "not triggered") ;
#endif
	      break ;

	    case OFC_HANDLE_FSSMB_OVERLAPPED:
	      hWaitQ = OfcFileGetOverlappedWaitQ (hEventHandle) ;
	      hEvent = BlueWaitQGetEventHandle (hWaitQ) ;
	      ofc_printf ("CIFS Overlapped: %s\n",
			   BlueWaitQempty (hWaitQ) ?
			   "triggered" : "not triggered") ;
	      break ;

	    case OFC_HANDLE_EVENT:
	      ofc_printf ("Event: %s\n",
			   ofc_event_test (hEventHandle) ?
			   "triggered" : "not triggered") ;
	      break ;

	    case OFC_HANDLE_TIMER:
	      ofc_printf ("Timer: %d msec\n",
                      BlueTimerGetWaitTime (hEventHandle)) ;
	      break ;
	    }
#if defined(OFC_APP_DEBUG)
	  BlueAppDump(BlueHandleGetApp (hEventHandle)) ;
#endif
	  
	}
      ofc_handle_unlock (handle) ;
    }
}

/** \} */
