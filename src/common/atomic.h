/*
 * atomic.h
 *
 * Copyright (c) 2011-2020 Aerospike, Inc. All rights reserved.
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

//==========================================================
// Includes.
//

#include <stdint.h>


//==========================================================
// Typedefs & constants.
//

typedef volatile uint64_t atomic64;
typedef volatile uint32_t atomic32;


//==========================================================
// Public API.
//

//------------------------------------------------
// 64-bit atomic operations.
//

// Add value b into 64-bit atomic integer a, and return result.
static inline int64_t
atomic64_add(atomic64 *a, int64_t b)
{
	int64_t i = b;

	__asm__ __volatile__ ("lock; xaddq %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return b + i;
}

#define atomic64_get(a)     (a)
#define atomic64_set(a, b)  (*(a) = (b))
#define atomic64_sub(a, b)  (atomic64_add((a), (0 - (b))))
#define atomic64_incr(a)    (atomic64_add((a), 1))
#define atomic64_decr(a)    (atomic64_add((a), -1))


//------------------------------------------------
// 32-bit atomic operations.
//

// Add value b into 32-bit atomic integer a, and return result.
static inline int64_t
atomic32_add(atomic32 *a, int32_t b)
{
	int32_t i = b;

	__asm__ __volatile__ ("lock; xadd %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return b + i;
}

#define atomic32_get(a)     (a)
#define atomic32_set(a, b)  (*(a) = (b))
#define atomic32_sub(a, b)  (atomic32_add((a), (0 - (b))))
#define atomic32_incr(a)    (atomic32_add((a), 1))
#define atomic32_decr(a)    (atomic32_add((a), -1))
