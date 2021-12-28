/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <sys/resource.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <sysexits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "ofc/types.h"
#include "ofc/handle.h"
#include "ofc/process.h"
#include "ofc/libc.h"
#include "ofc/thread.h"

#include "ofc/impl/consoleimpl.h"

#include "ofc/heap.h"

extern char **environ ;

BLUE_PROCESS_ID BlueProcessGetImpl (OFC_VOID)
{
  pid_t pid ;

  pid = getpid() ;
  return ((BLUE_PROCESS_ID) pid) ;
}

OFC_VOID BlueProcessBlockSignal (OFC_INT signal)
{
  sigset_t new_set ;
  sigset_t old_set ;

  sigemptyset (&new_set) ;
  sigaddset (&new_set, signal) ;

  pthread_sigmask (SIG_BLOCK, &new_set, &old_set) ;

}

OFC_VOID BlueProcessUnblockSignal (OFC_INT signal)
{
  sigset_t new_set ;

  sigemptyset (&new_set) ;
  sigaddset (&new_set, signal) ;

  pthread_sigmask (SIG_UNBLOCK, &new_set, OFC_NULL) ;
}

OFC_BOOL 
BlueProcessTermTrapImpl (BLUE_PROCESS_TRAP_HANDLER trap)
{
  struct sigaction action ;
  OFC_BOOL ret ;

  ret = OFC_FALSE ;
  sigemptyset (&action.sa_mask) ;
  action.sa_handler = trap ;
  action.sa_flags = 0 ;

  if (sigaction (SIGTERM, &action, OFC_NULL) == 0)
    ret = OFC_TRUE ;

  return (ret) ;
}

OFC_HANDLE BlueProcessExecImpl (OFC_CTCHAR *name,
                                OFC_TCHAR *uname,
                                OFC_INT argc,
                                OFC_CHAR **argv)
{
  OFC_HANDLE hProcess ;
  OFC_CHAR *cname ;
  OFC_CHAR **exec_argv ;
  OFC_INT i ;
  pid_t pid ;
  int ret ;
  OFC_CHAR *cuname ;
  struct passwd *user ;
  volatile pid_t pid2 ;

  cname = BlueCtstr2cstr (name) ;
  cuname = BlueCtstr2cstr (uname) ;
  exec_argv = BlueHeapMalloc (sizeof (OFC_CHAR *) * (argc+1)) ;
  for (i = 0 ; i < argc ; i++)
    exec_argv[i] = argv[i] ;
  exec_argv[i] = OFC_NULL ;

  hProcess = OFC_INVALID_HANDLE_VALUE ;

  pid = vfork () ;
  if (pid < 0)
    {
      BlueProcessCrash ("Unable to Fork First Process\n") ;
    }
  else if (pid == 0)
    {
      int ret2 ;

      /* We are the first Child */
      ret2 = EX_SOFTWARE ;
      pid2 = fork() ;
      if (pid2 < 0)
	{
	  BlueProcessCrash ("Unable to Fork Second Process\n") ;
	}
      else if (pid2 == 0)
	{
	  int ret3 ;

	  if (cuname != OFC_NULL)
	    {
	      user = getpwnam(cuname) ;
	      if (user != OFC_NULL)
		{
		  setgid (user->pw_gid) ;
		  setuid (user->pw_uid) ;
		}
	    }
	  /* We are the Daemon.  Exec the daemon */
	  ret3 = execve (cname, exec_argv, environ) ;
	  if (ret3 < 0)
	    BlueProcessCrash ("Unable to Exec the Daemon\n") ;
	  /*
	   * Although we exit with a return code, we are detached, so 
	   * no one is looking for it.  Not only that, but if execve was
	   * successful, it would not have returned
	   */
	  _Exit(EX_SOFTWARE) ;
	}
      else
	ret2 = EX_OK ;
      /*
       * This will be returned to the parent in his waitpid
       */
      _Exit(ret2) ;
    }
  else 
    {
      /* 
       * We are the original process, The first child will exit 
       * immediately after spawning the daemon
       */
      waitpid (pid, &ret, 0) ;
      if (ret == EX_OK)
	{
	  OFC_DWORD_PTR pid2l = (OFC_DWORD_PTR) pid2 ;
	  hProcess = 
	    ofc_handle_create (OFC_HANDLE_PROCESS, (OFC_VOID *) pid2l) ;
	}
    }

  BlueHeapFree (exec_argv) ;
  BlueHeapFree (cuname) ;
  BlueHeapFree (cname) ;

  return (hProcess) ;
}

BLUE_PROCESS_ID BlueProcessGetIdImpl (OFC_HANDLE hProcess)
{
  pid_t pid ;

  pid = (pid_t) ofc_handle_lock (hProcess) ;
  if (pid != (pid_t) 0)
    ofc_handle_unlock (hProcess) ;

  return ((BLUE_PROCESS_ID) pid) ;
}
  
OFC_VOID BlueProcessTermImpl (OFC_HANDLE hProcess)
{
  pid_t pid ;

  pid = (pid_t) ofc_handle_lock (hProcess) ;

  kill (pid, SIGTERM) ;

  ofc_handle_destroy (hProcess) ;
  ofc_handle_unlock (hProcess) ;
}

OFC_VOID BlueProcessKillImpl (BLUE_PROCESS_ID pid)
{
  kill (pid, SIGTERM) ;
}

OFC_VOID BlueProcessSetPriority (BLUE_PROCESS_PRIORITY prio) 
{
}

OFC_VOID
BlueProcessCrashImpl (OFC_CCHAR *obuf)
{
  BlueWriteConsoleImpl (obuf) ;
  _Exit(EX_SOFTWARE) ;
}  
