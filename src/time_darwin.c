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
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "ofc/types.h"
#include "ofc/time.h"
#include "ofc/impl/timeimpl.h"

#include "ofc/file.h"

/**
 * \defgroup BlueTimeDarwin Darwin Timer Interface
 * \ingroup BlueDarwin
 */

#define _SEC_IN_MINUTE 60L
#define _SEC_IN_HOUR 3600L
#define _SEC_IN_DAY 86400L

static const int _DAYS_BEFORE_MONTH[12] =
  {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

#define _ISLEAP(y) (((y) % 4) == 0 && \
		    (((y) % 100) != 0 || (((y)+1900) % 400) == 0))

#define _DAYS_IN_YEAR(year) (_ISLEAP(year) ? 366 : 365)

static BLUE_UINT32 BlueTimeMakeTime (struct tm *tm)
{
  BLUE_UINT32 tim ;
  long days ;
  int year ;

  /* 
   * Find number of seconds in the day
   */
  tim = tm->tm_sec + (tm->tm_min * _SEC_IN_MINUTE) +
    (tm->tm_hour * _SEC_IN_HOUR) ;
  /* 
   * find number of days in the year
   */
  days = (tm->tm_mday - 1) + _DAYS_BEFORE_MONTH[tm->tm_mon] ;
  /*
   * Check if this is a leap year and we're after feb 28
   */
  if (tm->tm_mon > 1 && _DAYS_IN_YEAR (tm->tm_year) == 366)
    days++;

  /* 
   * compute days in other years, base year is 1970 
   */
  if (tm->tm_year > 70)
    {
      for (year = 70; year < tm->tm_year; year++)
  	days += _DAYS_IN_YEAR (year);
    }

  /*
   * compute total seconds 
   */
  tim += (days * _SEC_IN_DAY);

  return tim;
}

static BLUE_VOID BlueTimeLocalTime (time_t time, struct tm *tm)
{
  time_t days_in_year ;
  time_t ticks_in_day ;
  time_t ticks_in_hour ;

  ticks_in_day = time % _SEC_IN_DAY ;
  /*
   * Now find year since 1970
   */
  tm->tm_year = 70 ;
  days_in_year = time / _SEC_IN_DAY ;
  while (days_in_year >= _DAYS_IN_YEAR(tm->tm_year))
    {
      days_in_year -= _DAYS_IN_YEAR(tm->tm_year) ;
      tm->tm_year++ ;
    }
  /*
   * We now have ticks_in_day, year, and days in year
   */
  /*
   * Check if this is a leap year and whether this is after feb 28
   */
  if (_DAYS_IN_YEAR(tm->tm_year) == 366)
    {
      if (days_in_year == _DAYS_BEFORE_MONTH[2])
	tm->tm_mon = 1 ;
      else 
	{
	  if (days_in_year > _DAYS_BEFORE_MONTH[2])
	    /*
	     * Since leap day is not in _DAYS_BEFORE_MONTH, 
	     * it's easiest to ignore it
	     */
	    days_in_year-- ;

	  /*
	   * Now lets find the month
	   */
	  tm->tm_mon = 0 ;
	  while ((tm->tm_mon < 11) &&
		 (days_in_year >= (_DAYS_BEFORE_MONTH[tm->tm_mon+1])))
	    tm->tm_mon++ ;
	}
    }
  else
    {
      /*
       * Now lets find the month
       */
      tm->tm_mon = 0 ;
      while ((tm->tm_mon < 11) &&
	     (days_in_year >= (_DAYS_BEFORE_MONTH[tm->tm_mon+1]))) 
	tm->tm_mon++ ;
    }

  tm->tm_mday = (int) days_in_year - _DAYS_BEFORE_MONTH[tm->tm_mon] + 1;
  /*
   * We now have year, month, day, and ticks_in_day
   */
  /*
   * Let's find hour, minute, second
   */
  tm->tm_hour = (int) ticks_in_day / _SEC_IN_HOUR ;
  ticks_in_hour = ticks_in_day % _SEC_IN_HOUR ;
  tm->tm_min = (int) ticks_in_hour / _SEC_IN_MINUTE ;
  tm->tm_sec = (int) ticks_in_hour % _SEC_IN_MINUTE ;
}

#undef DEBUG_WRAP
BLUE_MSTIME BlueTimeGetNowImpl(BLUE_VOID) 
{
  BLUE_MSTIME ms ;
  struct timeval tp ;
#if defined(DEBUG_WRAP)
  unsigned long ticks;
#endif

  gettimeofday (&tp, NULL) ;

#if defined(DEBUG_WRAP)
  ticks = (tp.tv_sec * 500) + (tp.tv_usec / 500) ;
#if 1
  /* for int32 */
  ms = (BLUE_MSTIME)(ticks) * 2+2146690000;
#else
  /* for int16 */
  ms = (BLUE_MSTIME)(ticks) * 2+32512;
#endif
#else
  ms = (BLUE_MSTIME)((tp.tv_sec * 1000) + (tp.tv_usec / 1000)) ;
#endif
  return (ms) ;
}

BLUE_VOID BlueTimeGetFileTimeImpl (BLUE_FILETIME *filetime)
{
  struct timespec tp ;
  /*
   * Get time in seconds
   */

  time(&tp.tv_sec) ;
  tp.tv_nsec = 0 ;

  EpochTimeToFileTime (tp.tv_sec, tp.tv_nsec, filetime) ;
}

BLUE_UINT16 BlueTimeGetTimeZoneImpl (BLUE_VOID)
{
  struct tm *gm ;
  time_t ts ;
  BLUE_UINT16 ret ;

  time(&ts) ;
  gm = localtime (&ts) ;

  /*
   * Returns it in seconds, we want minutes
   * this measures the number of minutes that we are behind GMT. 
   * What we want is the number of minutes that GMT is ahead of us.
   */
  ret = -1 * gm->tm_gmtoff / 60 ;

  return (ret) ;
}

BLUE_BOOL BlueFileTimeToDosDateTimeImpl (const BLUE_FILETIME *lpFileTime,
					 BLUE_WORD *lpFatDate,
					 BLUE_WORD *lpFatTime)
{
  struct timespec tp ;
  struct tm tm;

  FileTimeToEpochTime (lpFileTime, (BLUE_ULONG *) &tp.tv_sec, 
		       (BLUE_ULONG *) &tp.tv_nsec) ;

  BlueTimeLocalTime (tp.tv_sec, &tm) ;

  BlueTimeElementsToDosDateTime (tm.tm_mon + 1,
				 tm.tm_mday,
				 tm.tm_year + 1900,
				 tm.tm_hour,
				 tm.tm_min,
				 tm.tm_sec,
				 lpFatDate,
				 lpFatTime) ;

  return (BLUE_TRUE) ;
}

BLUE_BOOL BlueDosDateTimeToFileTimeImpl (BLUE_WORD FatDate, 
					 BLUE_WORD FatTime,
					 BLUE_FILETIME *lpFileTime)
{
  struct timespec tp ;
  struct tm tm;

  BLUE_UINT16 mon ;
  BLUE_UINT16 day ;
  BLUE_UINT16 year ;
  BLUE_UINT16 hour ;
  BLUE_UINT16 min ;
  BLUE_UINT16 sec ;

  BlueTimeDosDateTimeToElements (FatDate,
				 FatTime,
				 &mon,
				 &day,
				 &year,
				 &hour,
				 &min,
				 &sec) ;
  tm.tm_mon = mon - 1 ;
  tm.tm_mday = day ;
  tm.tm_year = year - 1900 ;
  tm.tm_hour = hour ;
  tm.tm_min = min ;
  tm.tm_sec = sec ;

  tp.tv_sec = BlueTimeMakeTime (&tm) ;
  tp.tv_nsec = 0 ;

  EpochTimeToFileTime (tp.tv_sec, tp.tv_nsec, lpFileTime) ;

  return (BLUE_TRUE) ;
}

BLUE_MSTIME BlueTimeGetRuntimeImpl (BLUE_VOID)
{
  int ret ;
  struct rusage r_usage ;
  BLUE_MSTIME runtime ;

  runtime = 0 ;
  ret = getrusage (RUSAGE_SELF, &r_usage) ;
  if (ret == 0)
    runtime = (BLUE_MSTIME) ((r_usage.ru_utime.tv_sec + 
			      r_usage.ru_stime.tv_sec) * 1000000 + 
			     (r_usage.ru_utime.tv_usec + 
			      r_usage.ru_stime.tv_usec)) ;
  return (runtime) ;
}

