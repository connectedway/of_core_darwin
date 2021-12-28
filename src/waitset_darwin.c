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

  DarwinWaitSet = BlueHeapMalloc (sizeof (DARWIN_WAIT_SET)) ;
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
  BlueHeapFree (pWaitSet->impl) ;
  pWaitSet->impl = OFC_NULL ;
}

typedef struct
{
  BLUE_HANDLE hEvent ;
  BLUE_HANDLE hAssoc ;
} EVENT_ELEMENT ;

OFC_VOID BlueWaitSetSignalImpl (BLUE_HANDLE handle, BLUE_HANDLE hEvent)
{
  WAIT_SET *pWaitSet ;
  DARWIN_WAIT_SET *DarwinWaitSet ;

  pWaitSet = BlueHandleLock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      DarwinWaitSet = pWaitSet->impl ;
      BlueHandleUnlock (handle) ;
      write (DarwinWaitSet->pipe_files[1], &hEvent, sizeof (BLUE_HANDLE)) ;
    }
}

OFC_VOID BlueWaitSetWakeImpl (BLUE_HANDLE handle)
{
  BlueWaitSetSignalImpl (handle, BLUE_HANDLE_NULL) ;
}

BLUE_HANDLE PollEvent (OFC_INT fd, BLUE_HANDLE eventQueue)
{
  EVENT_ELEMENT *eventElement ;
  BLUE_HANDLE hEvent ;
  BLUE_HANDLE triggered_event ;
  OFC_SIZET size ;
  OFC_BOOL wake ;
  /*
   * Special case.  It's the pipe.  Let's read the
   * event handle
   */
  triggered_event = BLUE_HANDLE_NULL ;
  wake = OFC_FALSE ;

  do
    {
      size = read (fd, &hEvent, sizeof(BLUE_HANDLE)) ;
      if (size == sizeof (BLUE_HANDLE))
	{
	  if (hEvent == BLUE_HANDLE_NULL)
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
  while (triggered_event == BLUE_HANDLE_NULL && !wake &&
	 size == sizeof (BLUE_HANDLE)) ;

  return (triggered_event);
}

BLUE_HANDLE BlueWaitSetWaitImpl (BLUE_HANDLE handle)
{
  WAIT_SET *pWaitSet ;
  DARWIN_WAIT_SET *DarwinWaitSet ;

  BLUE_HANDLE hEvent ;
  BLUE_HANDLE hEventHandle ;
  BLUE_HANDLE triggered_event ;
  BLUE_HANDLE timer_event ;
  BLUE_HANDLE darwinHandle ;
#if defined(OFC_FS_DARWIN)
  BLUE_HANDLE fsHandle ;
  BLUE_FS_TYPE fsType ;
#endif
  struct pollfd *darwin_handle_list ;
  BLUE_HANDLE *blue_handle_list ;

  nfds_t wait_count ;
  int wait_index ;
  int leastWait ;

  int poll_count ;
  OFC_MSTIME wait_time ;
  BLUE_HANDLE eventQueue ;
  EVENT_ELEMENT *eventElement ;
  BLUE_HANDLE hWaitQ ;

  triggered_event = BLUE_HANDLE_NULL ;
  pWaitSet = BlueHandleLock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      eventQueue = BlueQcreate () ;
      leastWait = OFC_MAX_SCHED_WAIT ;
      timer_event = BLUE_HANDLE_NULL ;

      wait_count = 0 ;
      darwin_handle_list = BlueHeapMalloc (sizeof (struct pollfd)) ;
      blue_handle_list = BlueHeapMalloc (sizeof (BLUE_HANDLE)) ;

      DarwinWaitSet = pWaitSet->impl ;

      /*
       * Purge any additional queued events.  We'll get these before we
       * sleep the next time
       */
      while (read (DarwinWaitSet->pipe_files[0], &hEventHandle,
		   sizeof (BLUE_HANDLE)) > 0) ;
      darwin_handle_list[wait_count].fd = DarwinWaitSet->pipe_files[0] ;
      darwin_handle_list[wait_count].events = POLLIN ;
      darwin_handle_list[wait_count].revents = 0 ;
      blue_handle_list[wait_count] = BLUE_HANDLE_NULL ;

      wait_count++ ;

      for (hEventHandle = 
	     (BLUE_HANDLE) BlueQfirst (pWaitSet->hHandleQueue) ;
	   hEventHandle != BLUE_HANDLE_NULL && 
	     triggered_event == BLUE_HANDLE_NULL ;
	   hEventHandle = 
	     (BLUE_HANDLE) BlueQnext (pWaitSet->hHandleQueue, 
				      (OFC_VOID *) hEventHandle) )
	{
	  switch (BlueHandleGetType(hEventHandle))
	    {
	    default:
	    case BLUE_HANDLE_WAIT_SET:
	    case BLUE_HANDLE_SCHED:
	    case BLUE_HANDLE_APP:
	    case BLUE_HANDLE_THREAD:
	    case BLUE_HANDLE_PIPE:
	    case BLUE_HANDLE_MAILSLOT:
	    case BLUE_HANDLE_FSWIN32_FILE:
	    case BLUE_HANDLE_FSDARWIN_FILE:
	    case BLUE_HANDLE_QUEUE:
	      /*
	       * These are not synchronizeable.  Simple ignore
	       */
	      break ;

	    case BLUE_HANDLE_WAIT_QUEUE:
	      hEvent = BlueWaitQGetEventHandle (hEventHandle) ;
	      if (!BlueWaitQempty (hEventHandle))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = BlueHeapMalloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case BLUE_HANDLE_FILE:
#if defined(OFC_FS_DARWIN)
	      fsType = OfcFileGetFSType(hEventHandle) ;

	      if (fsType == BLUE_FS_DARWIN)
		{
		  darwin_handle_list = 
		    BlueHeapRealloc (darwin_handle_list,
				     sizeof (struct pollfd) * (wait_count+1)) ;
		  blue_handle_list = 
		    BlueHeapRealloc (blue_handle_list,
				     sizeof (BLUE_HANDLE) * (wait_count+1)) ;
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
	    case BLUE_HANDLE_SOCKET:
	      /*
	       * Wait on event
	       */
	      darwin_handle_list = 
		BlueHeapRealloc (darwin_handle_list,
				 sizeof (struct pollfd) * (wait_count+1)) ;
	      blue_handle_list = 
		BlueHeapRealloc (blue_handle_list,
				 sizeof (BLUE_HANDLE) * (wait_count+1)) ;
	      
	      darwinHandle = BlueSocketGetImpl (hEventHandle) ;
	      darwin_handle_list[wait_count].fd = 
		BlueSocketImplGetFD (darwinHandle) ;
	      darwin_handle_list[wait_count].events = 
		BlueSocketImplGetEvent (darwinHandle) ;
	      darwin_handle_list[wait_count].revents = 0 ;
	      blue_handle_list[wait_count] = hEventHandle ;
	      wait_count++ ;
	      break ;

	    case BLUE_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
	      hEvent = OfcFSDarwinGetOverlappedEvent (hEventHandle) ;
	      if (ofc_event_test (hEvent))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = BlueHeapMalloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
#endif
	      break ;

	    case BLUE_HANDLE_FSCIFS_OVERLAPPED:
	      hWaitQ = OfcFileGetOverlappedWaitQ (hEventHandle) ;
	      hEvent = BlueWaitQGetEventHandle (hWaitQ) ;
	      if (!BlueWaitQempty (hWaitQ))
		{
		  triggered_event = hEventHandle ;
		}
	      else
		{
		  eventElement = BlueHeapMalloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEvent ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case BLUE_HANDLE_EVENT:
	      if (ofc_event_test (hEventHandle))
		{
		  triggered_event = hEventHandle ;
		  if (ofc_event_get_type (hEventHandle) == OFC_EVENT_AUTO)
		    ofc_event_reset (hEventHandle) ;
		}
	      else
		{
		  eventElement = BlueHeapMalloc (sizeof (EVENT_ELEMENT)) ;
		  eventElement->hAssoc = hEventHandle ;
		  eventElement->hEvent = hEventHandle ;
		  BlueQenqueue (eventQueue, eventElement) ;
		}
	      break ;

	    case BLUE_HANDLE_TIMER:
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

      BlueHandleUnlock (handle) ;

      if (triggered_event == BLUE_HANDLE_NULL)
	{
	  poll_count = poll (darwin_handle_list, wait_count, leastWait) ;
	  if (poll_count == 0 && timer_event != BLUE_HANDLE_NULL)
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
	BlueHeapFree (eventElement) ;

      BlueQdestroy (eventQueue) ;

      BlueHeapFree (darwin_handle_list) ;
      BlueHeapFree (blue_handle_list) ;

    }
  return (triggered_event) ;
}

OFC_VOID BlueWaitSetSetAssocImpl (BLUE_HANDLE hEvent, 
				   BLUE_HANDLE hApp, BLUE_HANDLE hSet)
{
  BLUE_HANDLE hAssoc ;

  switch (BlueHandleGetType(hEvent))
    {
    default:
    case BLUE_HANDLE_WAIT_SET:
    case BLUE_HANDLE_SCHED:
    case BLUE_HANDLE_APP:
    case BLUE_HANDLE_THREAD:
    case BLUE_HANDLE_PIPE:
    case BLUE_HANDLE_MAILSLOT:
    case BLUE_HANDLE_FSWIN32_FILE:
    case BLUE_HANDLE_QUEUE:
      /*
       * These are not synchronizeable.  Simple ignore
       */
      break ;

    case BLUE_HANDLE_WAIT_QUEUE:
      hAssoc = BlueWaitQGetEventHandle (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
      break ;

    case BLUE_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
      hAssoc = OfcFSDarwinGetOverlappedEvent (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
#endif
      break ;

    case BLUE_HANDLE_FSCIFS_OVERLAPPED:
      hAssoc = OfcFileGetOverlappedEvent (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
      break ;

    case BLUE_HANDLE_EVENT:
    case BLUE_HANDLE_FILE:
    case BLUE_HANDLE_SOCKET:
    case BLUE_HANDLE_TIMER:
      /*
       * These don't need to set associated events
       */
      break ;
    }
}
OFC_VOID BlueWaitSetAddImpl (BLUE_HANDLE hSet, BLUE_HANDLE hApp,
			      BLUE_HANDLE hEvent) 
{
  BLUE_HANDLE hAssoc ;

  switch (BlueHandleGetType(hEvent))
    {
    default:
    case BLUE_HANDLE_WAIT_SET:
    case BLUE_HANDLE_SCHED:
    case BLUE_HANDLE_APP:
    case BLUE_HANDLE_THREAD:
    case BLUE_HANDLE_PIPE:
    case BLUE_HANDLE_MAILSLOT:
    case BLUE_HANDLE_FSWIN32_FILE:
    case BLUE_HANDLE_QUEUE:
      /*
       * These are not synchronizeable.  Simple ignore
       */
      break ;

    case BLUE_HANDLE_WAIT_QUEUE:
      hAssoc = BlueWaitQGetEventHandle (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
      if (!BlueWaitQempty (hEvent))
	BlueWaitSetSignalImpl (hSet, hAssoc) ;
      break ;

    case BLUE_HANDLE_EVENT:
      BlueHandleSetApp (hEvent, hApp, hSet) ;
      if (ofc_event_test (hEvent))
	BlueWaitSetSignalImpl (hSet, hEvent) ;
      break ;

    case BLUE_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
      hAssoc = OfcFSDarwinGetOverlappedEvent (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
      if (ofc_event_test (hAssoc))
	BlueWaitSetSignalImpl (hSet, hAssoc) ;
#endif
      break ;

    case BLUE_HANDLE_FSCIFS_OVERLAPPED:
      hAssoc = OfcFileGetOverlappedEvent (hEvent) ;
      BlueHandleSetApp (hAssoc, hApp, hSet) ;
      if (ofc_event_test (hAssoc))
	{
	  BlueWaitSetSignalImpl (hSet, hAssoc) ;
	}
      break ;

    case BLUE_HANDLE_FILE:
    case BLUE_HANDLE_SOCKET:
    case BLUE_HANDLE_TIMER:
      /*
       * These don't need to set associated events
       */
      break ;
    }
}

OFC_VOID BlueWaitSetDebug (BLUE_HANDLE handle)
{
  WAIT_SET *pWaitSet ;

  BLUE_HANDLE hEvent ;
  BLUE_HANDLE hEventHandle ;
  BLUE_HANDLE hWaitQ ;
#if defined(OFC_FS_DARWIN)
  BLUE_FS_TYPE fsType ;
#endif

  pWaitSet = BlueHandleLock (handle) ;

  if (pWaitSet != OFC_NULL)
    {
      for (hEventHandle = 
	     (BLUE_HANDLE) BlueQfirst (pWaitSet->hHandleQueue) ;
	   hEventHandle != BLUE_HANDLE_NULL ;
	   hEventHandle = 
	     (BLUE_HANDLE) BlueQnext (pWaitSet->hHandleQueue, 
				      (OFC_VOID *) hEventHandle) )
	{
	  switch (BlueHandleGetType(hEventHandle))
	    {
	    default:
	    case BLUE_HANDLE_WAIT_SET:
	    case BLUE_HANDLE_SCHED:
	    case BLUE_HANDLE_APP:
	    case BLUE_HANDLE_THREAD:
	    case BLUE_HANDLE_PIPE:
	    case BLUE_HANDLE_MAILSLOT:
	    case BLUE_HANDLE_FSWIN32_FILE:
	    case BLUE_HANDLE_FSDARWIN_FILE:
	    case BLUE_HANDLE_QUEUE:
	      /*
	       * These are not synchronizeable.  Simple ignore
	       */
	      break ;

	    case BLUE_HANDLE_WAIT_QUEUE:
	      BlueCprintf ("Wait Queue: %s\n",
			   BlueWaitQempty(hEventHandle) ? 
			   "empty" : "not empty") ;
	      break ;

	    case BLUE_HANDLE_FILE:
#if defined(OFC_FS_DARWIN)
	      fsType = OfcFileGetFSType(hEventHandle) ;
	      if (fsType == BLUE_FS_DARWIN)
		{
		  BlueCprintf ("Darwin File\n") ;
		}
#endif
	      break ;

	    case BLUE_HANDLE_SOCKET:
	      BlueCprintf ("Darwin Socket\n") ;
	      break ;

	    case BLUE_HANDLE_FSDARWIN_OVERLAPPED:
#if defined(OFC_FS_DARWIN)
	      hEvent = OfcFSDarwinGetOverlappedEvent (hEventHandle) ;
	      BlueCprintf ("Darwin Overlapped: %s\n",
			   ofc_event_test (hEvent) ? 
			   "triggered" : "not triggered") ;
#endif
	      break ;

	    case BLUE_HANDLE_FSCIFS_OVERLAPPED:
	      hWaitQ = OfcFileGetOverlappedWaitQ (hEventHandle) ;
	      hEvent = BlueWaitQGetEventHandle (hWaitQ) ;
	      BlueCprintf ("CIFS Overlapped: %s\n",
			   BlueWaitQempty (hWaitQ) ?
			   "triggered" : "not triggered") ;
	      break ;

	    case BLUE_HANDLE_EVENT:
	      BlueCprintf ("Event: %s\n",
			   ofc_event_test (hEventHandle) ?
			   "triggered" : "not triggered") ;
	      break ;

	    case BLUE_HANDLE_TIMER:
	      BlueCprintf ("Timer: %d msec\n", 
			   BlueTimerGetWaitTime (hEventHandle)) ;
	      break ;
	    }
#if defined(OFC_APP_DEBUG)
	  BlueAppDump(BlueHandleGetApp (hEventHandle)) ;
#endif
	  
	}
      BlueHandleUnlock (handle) ;
    }
}

/** \} */
