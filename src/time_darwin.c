/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "ofc/types.h"
#include "ofc/time.h"
#include "ofc/impl/timeimpl.h"

#include "ofc/file.h"

/**
 * \defgroup time_darwin Darwin Timer Interface
 * \ingroup darwin
 */

#define _SEC_IN_MINUTE 60L
#define _SEC_IN_HOUR 3600L
#define _SEC_IN_DAY 86400L

static const int _DAYS_BEFORE_MONTH[12] =
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

#define _ISLEAP(y) (((y) % 4) == 0 && \
            (((y) % 100) != 0 || (((y)+1900) % 400) == 0))

#define _DAYS_IN_YEAR(year) (_ISLEAP(year) ? 366 : 365)

static OFC_UINT32 ofc_time_make_time(struct tm *tm) {
    OFC_UINT32 tim;
    long days;
    int year;

    /*
     * Find number of seconds in the day
     */
    tim = tm->tm_sec + (tm->tm_min * _SEC_IN_MINUTE) +
          (tm->tm_hour * _SEC_IN_HOUR);
    /*
     * find number of days in the year
     */
    days = (tm->tm_mday - 1) + _DAYS_BEFORE_MONTH[tm->tm_mon];
    /*
     * Check if this is a leap year and we're after feb 28
     */
    if (tm->tm_mon > 1 && _DAYS_IN_YEAR (tm->tm_year) == 366)
        days++;

    /*
     * compute days in other years, base year is 1970
     */
    if (tm->tm_year > 70) {
        for (year = 70; year < tm->tm_year; year++)
            days += _DAYS_IN_YEAR (year);
    }

    /*
     * compute total seconds
     */
    tim += (days * _SEC_IN_DAY);

    return tim;
}

static OFC_VOID ofc_time_local_time(time_t time, struct tm *tm) {
    time_t days_in_year;
    time_t ticks_in_day;
    time_t ticks_in_hour;

    ticks_in_day = time % _SEC_IN_DAY;
    /*
     * Now find year since 1970
     */
    tm->tm_year = 70;
    days_in_year = time / _SEC_IN_DAY;
    while (days_in_year >= _DAYS_IN_YEAR(tm->tm_year)) {
        days_in_year -= _DAYS_IN_YEAR(tm->tm_year);
        tm->tm_year++;
    }
    /*
     * We now have ticks_in_day, year, and days in year
     */
    /*
     * Check if this is a leap year and whether this is after feb 28
     */
    if (_DAYS_IN_YEAR(tm->tm_year) == 366) {
        if (days_in_year == _DAYS_BEFORE_MONTH[2])
            tm->tm_mon = 1;
        else {
            if (days_in_year > _DAYS_BEFORE_MONTH[2])
                /*
                 * Since leap day is not in _DAYS_BEFORE_MONTH,
                 * it's easiest to ignore it
                 */
                days_in_year--;

            /*
             * Now lets find the month
             */
            tm->tm_mon = 0;
            while ((tm->tm_mon < 11) &&
                   (days_in_year >= (_DAYS_BEFORE_MONTH[tm->tm_mon + 1])))
                tm->tm_mon++;
        }
    } else {
        /*
         * Now lets find the month
         */
        tm->tm_mon = 0;
        while ((tm->tm_mon < 11) &&
               (days_in_year >= (_DAYS_BEFORE_MONTH[tm->tm_mon + 1])))
            tm->tm_mon++;
    }

    tm->tm_mday = (int) days_in_year - _DAYS_BEFORE_MONTH[tm->tm_mon] + 1;
    /*
     * We now have year, month, day, and ticks_in_day
     */
    /*
     * Let's find hour, minute, second
     */
    tm->tm_hour = (int) ticks_in_day / _SEC_IN_HOUR;
    ticks_in_hour = ticks_in_day % _SEC_IN_HOUR;
    tm->tm_min = (int) ticks_in_hour / _SEC_IN_MINUTE;
    tm->tm_sec = (int) ticks_in_hour % _SEC_IN_MINUTE;
}

#undef DEBUG_WRAP

OFC_MSTIME ofc_time_get_now_impl(OFC_VOID) {
    OFC_MSTIME ms;
    struct timeval tp;
#if defined(DEBUG_WRAP)
    unsigned long ticks;
#endif

    gettimeofday(&tp, NULL);

#if defined(DEBUG_WRAP)
    ticks = (tp.tv_sec * 500) + (tp.tv_usec / 500) ;
#if 1
    /* for int32 */
    ms = (OFC_MSTIME)(ticks) * 2+2146690000;
#else
    /* for int16 */
    ms = (OFC_MSTIME)(ticks) * 2+32512;
#endif
#else
    ms = (OFC_MSTIME) ((tp.tv_sec * 1000) + (tp.tv_usec / 1000));
#endif
    return (ms);
}

OFC_VOID ofc_time_get_file_time_impl(OFC_FILETIME *filetime) {
    struct timespec tp;
    /*
     * Get time in seconds
     */

    time(&tp.tv_sec);
    tp.tv_nsec = 0;

    epoch_time_to_file_time(tp.tv_sec, tp.tv_nsec, filetime);
}

OFC_UINT16 ofc_time_get_timezone_impl(OFC_VOID) {
    struct tm *gm;
    time_t ts;
    OFC_UINT16 ret;

    time(&ts);
    gm = localtime(&ts);

    /*
     * Returns it in seconds, we want minutes
     * this measures the number of minutes that we are behind GMT.
     * What we want is the number of minutes that GMT is ahead of us.
     */
    ret = -1 * gm->tm_gmtoff / 60;

    return (ret);
}

OFC_BOOL ofc_file_time_to_dos_date_time_impl(const OFC_FILETIME *lpFileTime,
                                             OFC_WORD *lpFatDate,
                                             OFC_WORD *lpFatTime) {
    struct timespec tp;
    struct tm tm;

    file_time_to_epoch_time(lpFileTime, (OFC_ULONG *) &tp.tv_sec,
                            (OFC_ULONG *) &tp.tv_nsec);

    ofc_time_local_time(tp.tv_sec, &tm);

    ofc_time_elements_to_dos_date_time(tm.tm_mon + 1,
                                       tm.tm_mday,
                                       tm.tm_year + 1900,
                                       tm.tm_hour,
                                       tm.tm_min,
                                       tm.tm_sec,
                                       lpFatDate,
                                       lpFatTime);

    return (OFC_TRUE);
}

OFC_BOOL ofc_dos_date_time_to_file_time_impl(OFC_WORD FatDate,
                                             OFC_WORD FatTime,
                                             OFC_FILETIME *lpFileTime) {
    struct timespec tp;
    struct tm tm;

    OFC_UINT16 mon;
    OFC_UINT16 day;
    OFC_UINT16 year;
    OFC_UINT16 hour;
    OFC_UINT16 min;
    OFC_UINT16 sec;

    ofc_dos_date_time_to_elements(FatDate,
                                  FatTime,
                                  &mon,
                                  &day,
                                  &year,
                                  &hour,
                                  &min,
                                  &sec);
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_year = year - 1900;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    tp.tv_sec = ofc_time_make_time(&tm);
    tp.tv_nsec = 0;

    epoch_time_to_file_time(tp.tv_sec, tp.tv_nsec, lpFileTime);

    return (OFC_TRUE);
}

OFC_MSTIME ofc_get_runtime_impl(OFC_VOID) {
    int ret;
    struct rusage r_usage;
    OFC_MSTIME runtime;

    runtime = 0;
    ret = getrusage(RUSAGE_SELF, &r_usage);
    if (ret == 0)
        runtime = (OFC_MSTIME) ((r_usage.ru_utime.tv_sec +
                                 r_usage.ru_stime.tv_sec) * 1000000 +
                                (r_usage.ru_utime.tv_usec +
                                 r_usage.ru_stime.tv_usec));
    return (runtime);
}

