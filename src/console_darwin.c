/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
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

OFC_INT g_fd = -1 ;

static OFC_VOID open_log(OFC_VOID)
{
#if defined(LOG_TO_FILE)
  g_fd = open (LOG_FILE, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
#else
  g_fd = STDOUT_FILENO ;
#endif
}

OFC_VOID BlueWriteStdOutImpl (OFC_CCHAR *obuf, OFC_SIZET len)
{
  if (g_fd == -1)
    open_log() ;
  write (g_fd, obuf, len) ;
  fsync (g_fd) ;
}

OFC_VOID BlueWriteConsoleImpl (OFC_CCHAR *obuf)
{
  if (g_fd == -1)
    open_log();
  write (g_fd, obuf, BlueCstrlen(obuf)) ;
  fsync (g_fd) ;
}

OFC_VOID BlueReadStdInImpl (OFC_CHAR *inbuf, OFC_SIZET len)
{
  fgets (inbuf, (int) len, stdin) ;
  if (BlueCstrlen (inbuf) < len)
    len = BlueCstrlen (inbuf) ;
  inbuf[len-1] = '\0' ;
}

OFC_VOID BlueReadPasswordImpl (OFC_CHAR *inbuf, OFC_SIZET len)
{
  char *pass ;

  pass = getpass ("") ;
  BlueCstrncpy (inbuf, pass, len-1) ;
  inbuf[len] = '\0' ;
}

/** \} */
