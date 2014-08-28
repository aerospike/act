/*
 *  Aerospike Foundation
 *  include/clock.h - simple clock helper functions
 *
 * Copyright (c) 2008-2012 Aerospike, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include <stdint.h>

/* Timekeeping */
typedef uint64_t cf_clock;
extern cf_clock cf_getms();
extern cf_clock cf_clock_getabsolute();
extern cf_clock cf_get_seconds();

inline static uint64_t
TIMESPEC_TO_MS_P( struct timespec *ts )
{
    uint64_t r1 = ts->tv_nsec;
    r1 /= 1000000;
    uint64_t r2 = ts->tv_sec;
    r2 *= 1000;
    return( r1 + r2 );
}

inline static uint64_t
TIMESPEC_TO_MS( struct timespec ts )
{
    uint64_t r1 = ts.tv_nsec;
    r1 /= 1000000;
    uint64_t r2 = ts.tv_sec;
    r2 *= 1000;
    return ( r1 + r2 );
}

inline static uint64_t
TIMESPEC_TO_US( struct timespec ts )
{
    uint64_t r1 = ts.tv_nsec;
    r1 /= 1000;
    uint64_t r2 = ts.tv_sec;
    r2 *= 1000000;
    return ( r1 + r2 );
}

inline static void
TIMESPEC_ADD_MS(struct timespec *ts, uint64_t ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000;
    if (ts->tv_nsec > 1000000000) {
        ts->tv_sec ++;
        ts->tv_nsec -= 1000000000;
    }
}


