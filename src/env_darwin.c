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

#include <stdlib.h>

#include "ofc/types.h"
#include "ofc/env.h"
#include "ofc/libc.h"

#include "ofc/heap.h"

static const char *env2str[BLUE_ENV_NUM] =
  {
    "BLUE_SHARE_HOME",
    "BLUE_SHARE_INSTALL",
    "BLUE_SHARE_ROOT"
  } ;

BLUE_BOOL 
BlueEnvGetImpl (BLUE_ENV_VALUE value, BLUE_TCHAR *ptr, BLUE_SIZET len) 
{
  char *env ;
  BLUE_BOOL ret ;
  BLUE_TCHAR *path ;

  /*
   * Darwin does not support reentrant getenv
   */
  ret = BLUE_FALSE ;
  if (ptr != BLUE_NULL && value < BLUE_ENV_NUM)
    {
      env = getenv (env2str[value]) ;
      if (env != NULL)
	{
	  path = BlueCcstr2tstr (env) ;
	  BlueCtstrncpy (ptr, path, len) ;
	  BlueHeapFree (path) ;
	  ret = BLUE_TRUE ;
	}

    }
  return (ret) ;
}

