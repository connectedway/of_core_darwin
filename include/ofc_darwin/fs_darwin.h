/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#if !defined(__BLUE_FSDARWIN_H__)
#define __BLUE_FSDARWIN_H__

#include "ofc/types.h"
#include "ofc/file.h"

#define BLUE_FS_DARWIN_BLOCK_SIZE 512

/**
 * \defgroup BlueFSDarwin Darwin File System Dependent Support
 * \ingroup BlueFS
 */

/** \{ */

#if defined(__cplusplus)
extern "C"
{
#endif
  BLUE_VOID BlueFSDarwinDestroyOverlapped (BLUE_HANDLE hOverlapped) ;
  BLUE_VOID 
  BlueFSDarwinSetOverlappedOffset (BLUE_HANDLE hOverlapped, BLUE_OFFT offset) ;
  BLUE_VOID BlueFSDarwinStartup (BLUE_VOID) ;
  BLUE_VOID BlueFSDarwinShutdown (BLUE_VOID);
  int BlueFSDarwinGetFD (BLUE_HANDLE) ;
  BLUE_HANDLE BlueFSDarwinGetOverlappedEvent (BLUE_HANDLE hOverlapped) ;
#if defined(__cplusplus)
}
#endif

#endif

/** \} */
