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
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ofc/types.h"
#include "ofc/impl/consoleimpl.h"
#include "ofc/libc.h"

/**
 * \defgroup BlueConsoleDarwin Darwin Console Interface
 * \ingroup BlueDarwin
 */

/** \{ */

#undef LOG_TO_FILE
#define LOG_FILE "/Users/rschmitt/bin/connectedsmb.log"

BLUE_INT g_fd = -1 ;

static BLUE_VOID open_log(BLUE_VOID)
{
#if defined(LOG_TO_FILE)
  g_fd = open (LOG_FILE, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
#else
  g_fd = STDOUT_FILENO ;
#endif
}

BLUE_VOID BlueWriteStdOutImpl (BLUE_CCHAR *obuf, BLUE_SIZET len)
{
  if (g_fd == -1)
    open_log() ;
  write (g_fd, obuf, len) ;
  fsync (g_fd) ;
}

BLUE_VOID BlueWriteConsoleImpl (BLUE_CCHAR *obuf)
{
  if (g_fd == -1)
    open_log();
  write (g_fd, obuf, BlueCstrlen(obuf)) ;
  fsync (g_fd) ;
}

BLUE_VOID BlueReadStdInImpl (BLUE_CHAR *inbuf, BLUE_SIZET len)
{
  fgets (inbuf, (int) len, stdin) ;
  if (BlueCstrlen (inbuf) < len)
    len = BlueCstrlen (inbuf) ;
  inbuf[len-1] = '\0' ;
}

BLUE_VOID BlueReadPasswordImpl (BLUE_CHAR *inbuf, BLUE_SIZET len)
{
  char *pass ;

  pass = getpass ("") ;
  BlueCstrncpy (inbuf, pass, len-1) ;
  inbuf[len] = '\0' ;
}

/** \} */
