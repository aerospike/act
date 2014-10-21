/*
 * atomic.h
 *
 * Copyright (c) 2008-2014 Aerospike, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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

#define MARCH_x86_64

#ifdef MARCH_x86_64

//------------------------------------------------
// 64-bit atomic operations.
//

typedef volatile uint64_t cf_atomic64;

#define cf_atomic64_get(a)		(a)
#define cf_atomic64_set(a, b)	(*(a) = (b))

// Add value b into 64-bit atomic integer a, and return result.
static inline int64_t
cf_atomic64_add(cf_atomic64 *a, int64_t b)
{
	int64_t i = b;

	__asm__ __volatile__ ("lock; xaddq %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return b + i;
}

#define cf_atomic64_sub(a, b)	(cf_atomic64_add((a), (0 - (b))))
#define cf_atomic64_incr(a)		(cf_atomic64_add((a), 1))
#define cf_atomic64_decr(a)		(cf_atomic64_add((a), -1))

// Until we switch usage of cf_atomic_int to cf_atomic64 in the next check-in:

typedef volatile uint64_t cf_atomic_int;

#define cf_atomic_int_get(a)	cf_atomic64_get(a)
#define cf_atomic_int_set(a, b)	cf_atomic64_set(a, b)
#define cf_atomic_int_add(a, b)	cf_atomic64_add(a, b)
#define cf_atomic_int_sub(a, b)	cf_atomic64_sub(a, b)
#define cf_atomic_int_incr(a)	cf_atomic64_add((a), 1)
#define cf_atomic_int_decr(a)	cf_atomic64_add((a), -1)


//------------------------------------------------
// 32-bit atomic operations.
//

typedef volatile uint32_t cf_atomic32;

#define cf_atomic32_get(a)		(a)
#define cf_atomic32_set(a, b)	(*(a) = (b))

// Add value b into 32-bit atomic integer a, and return result.
static inline int64_t
cf_atomic32_add(cf_atomic32 *a, int32_t b)
{
	int32_t i = b;

	__asm__ __volatile__ ("lock; xadd %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return b + i;
}

#define cf_atomic32_sub(a, b)	(cf_atomic32_add((a), (0 - (b))))
#define cf_atomic32_incr(a)		(cf_atomic32_add((a), 1))
#define cf_atomic32_decr(a)		(cf_atomic32_add((a), -1))

#endif // MARCH_x86_64
