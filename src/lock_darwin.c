/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <pthread.h>

#include "ofc/types.h"
#include "ofc/lock.h"
#include "ofc/heap.h"

typedef struct
{
  OFC_DWORD_PTR caller ;
  OFC_UINT32 thread ;
  pthread_mutexattr_t mutex_attr ;
  pthread_mutex_t mutex_lock ;
} BLUE_LOCK_IMPL ;

OFC_VOID BlueLockDestroyImpl (BLUE_LOCK_IMPL *lock)
{
  pthread_mutex_destroy (&lock->mutex_lock) ;
  pthread_mutexattr_destroy (&lock->mutex_attr) ;
  BlueHeapFree(lock);
}

OFC_VOID *BlueLockInitImpl (OFC_VOID)
{
  BLUE_LOCK_IMPL *lock;

  lock = BlueHeapMalloc(sizeof(BLUE_LOCK_IMPL));
  pthread_mutexattr_init (&lock->mutex_attr) ;
  pthread_mutexattr_settype (&lock->mutex_attr, PTHREAD_MUTEX_RECURSIVE) ;
  pthread_mutex_init (&lock->mutex_lock, &lock->mutex_attr) ;
  return (lock);
}

OFC_BOOL BlueLockTryImpl (BLUE_LOCK_IMPL *lock)
{
  OFC_BOOL ret ;
  
  ret = OFC_FALSE ;
  if (pthread_mutex_trylock (&lock->mutex_lock) == 0)
    ret = OFC_TRUE ;

  return (ret) ;
}

OFC_VOID BlueLockImpl (BLUE_LOCK_IMPL *lock)
{
  pthread_mutex_lock (&lock->mutex_lock) ;
}

OFC_VOID BlueUnlockImpl (BLUE_LOCK_IMPL *lock)
{
  pthread_mutex_unlock (&lock->mutex_lock) ;
}

