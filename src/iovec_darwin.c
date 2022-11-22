/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#define __OFC_CORE_DLL__

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>

#include "ofc/core.h"
#include "ofc/types.h"
#include "ofc/config.h"
#include "ofc/libc.h"
#include "ofc/heap.h"
#include "ofc/process.h"
#include "ofc/iovec.h"

OFC_VOID ofc_iovec_get(OFC_IOMAP inp, OFC_VOID **iovec_arg,
                       OFC_INT *veclen)
{
  struct iovec **iovec = iovec_arg;
  struct iovec_list *list = inp;
  OFC_INT i;
  OFC_INT j;
  
  *veclen = 0;
  for (i = 0 ; i < list->num_vecs; i++)
    {
      if (list->iovecs[i].type != IOVEC_ALLOC_NONE)
        (*veclen)++;
    }

  *iovec = ofc_malloc(sizeof (struct iovec) * (*veclen));
  j = 0;
  
  for (i = 0 ; i < list->num_vecs; i++)
    {
      if (list->iovecs[i].type != IOVEC_ALLOC_NONE)
        {
          (*iovec)[j].iov_base = list->iovecs[i].data;
          (*iovec)[j].iov_len = list->iovecs[i].length;
          j++;
        }
    }
}
