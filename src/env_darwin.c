/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <stdlib.h>

#include "ofc/types.h"
#include "ofc/env.h"
#include "ofc/libc.h"

#include "ofc/heap.h"

static const char *env2str[BLUE_ENV_NUM] =
  {
    "OPEN_FILES_HOME",
    "OPEN_FILES_INSTALL",
    "OPEN_FILES_ROOT"
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

