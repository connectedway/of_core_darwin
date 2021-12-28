/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#if !defined(__OFC_FSDARWIN_H__)
#define __OFC_FSDARWIN_H__

#include "ofc/types.h"
#include "ofc/file.h"

#define OFC_FS_DARWIN_BLOCK_SIZE 512

/**
 * \defgroup BlueFSDarwin Darwin File System Dependent Support
 * \ingroup BlueFS
 */

/** \{ */

#if defined(__cplusplus)
extern "C"
{
#endif
  OFC_VOID OfcFSDarwinDestroyOverlapped (BLUE_HANDLE hOverlapped) ;
  OFC_VOID 
  OfcFSDarwinSetOverlappedOffset (BLUE_HANDLE hOverlapped, OFC_OFFT offset) ;
  OFC_VOID OfcFSDarwinStartup (OFC_VOID) ;
  OFC_VOID OfcFSDarwinShutdown (OFC_VOID);
  int OfcFSDarwinGetFD (BLUE_HANDLE) ;
  BLUE_HANDLE OfcFSDarwinGetOverlappedEvent (BLUE_HANDLE hOverlapped) ;
#if defined(__cplusplus)
}
#endif

#endif

/** \} */
