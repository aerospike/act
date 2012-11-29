/*
 *  Aerospike Foundation
 *  include/atomic.h - atomic memory operations
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
 *
 */
#pragma once

#include <stdint.h>

/* SYNOPSIS
 * Atomic memory operations
 * Memory barriers
 * > */
 
#define MARCH_x86_64

/* Atomic memory operations */

/* cf_atomic64
 * An implementation of an atomic 64-bit data type
 * NB: some older compilers may exhibit aliasing bugs that require this
 * definition to be wrapped in a structure */

#ifdef MARCH_i686
typedef volatile uint32_t cf_atomic32;
typedef volatile uint32_t cf_atomic_p;
typedef volatile uint32_t cf_atomic_int;
#define SIZEOF_ATOMIC_INT 4
typedef uint32_t cf_atomic_int_t; // the point here is a type of the same size as cf_atomic_int but isn't atomic
#define HAS_ATOMIC_32 1
#endif

#ifdef MARCH_x86_64
typedef volatile uint64_t cf_atomic64;
typedef volatile uint32_t cf_atomic32;
typedef volatile uint64_t cf_atomic_p;
typedef volatile uint64_t cf_atomic_int;
#define SIZEOF_ATOMIC_INT 8
typedef uint64_t cf_atomic_int_t; // the point here is a type of the same size as cf_atomic_int but isn't atomic
#define HAS_ATOMIC_32 1
#define HAS_ATOMIC_64 1
#endif



/*
 * cf_atomicX_add
 * Atomic addition: add a value b into an atomic integer a, and return the result
 *
 * cf_atomicX_cas
 * Compare-and-swap: test a value b against an atomic integer a; if they
 * are equal, store the value x into a, and return the initial value of a.
 * "Success" can be checked by comparing the returned value against b
 * NB: this is a strong memory barrier
 *
 * cf_atomicX_fas
 * Fetch-and-swap: swap the values of  b and a 
 * 
 * cf_atomicX_addunless
 * Increment-unless: test a value b against an atomic integer a.  If they
 * are NOT equal, add x to a, and return non-zero; if they ARE equal, return
 * zero 
 */


/* cf_atomic64_get
 * Read an atomic value */
#ifdef HAS_ATOMIC_64

#define cf_atomic64_get(a) (a)
#define cf_atomic64_set(a, b) (*(a) = (b))

static inline int64_t
cf_atomic64_add(cf_atomic64 *a, int64_t b)
{
	int64_t i = b;

	__asm__ __volatile__ ("lock; xaddq %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return(b + i);
}
#define cf_atomic64_sub(a,b) (cf_atomic64_add((a), (0 - (b))))
#define cf_atomic64_incr(a) (cf_atomic64_add((a), 1))
#define cf_atomic64_decr(a) (cf_atomic64_add((a), -1))

static inline int64_t
cf_atomic64_cas(cf_atomic64 *a, int64_t b, int64_t x)
{
	int64_t p;

	__asm__ __volatile__ ("lock; cmpxchgq %1,%2" : "=a"(p) : "q"(x), "m"(*(a)), "0"(b) : "memory");

	return(p);
}

#define cf_atomic64_cas_m(_a, _b, _x) \
({  __typeof__(_b) __b = _b; \
	__asm__ __volatile__ ("lock; cmpxchgq %1,%2" : "=a"(__b) : "q"(_x), "m"(*(_a)), "0"(_b) : "memory"); \
	__b; \
})

/* cf_atomic64_fas
 * Fetch-and-swap: swap the values of  b and a */
static inline int64_t
cf_atomic64_fas(cf_atomic64 *a, cf_atomic64 *b)
{
	int64_t p;

	__asm__ __volatile__ ("lock; xchgq %0,%1" : "=r"(p) : "m"(*(a)), "0"(*(b)) : "memory");

	return(p);
}

#define cf_atomic64_fas_m(_a, _b) \
({  __typeof__(_b) __b; \
	__asm__ __volatile__ ("lock; xchgq %0,%1" : "=r"(__b) : "m"(*(_a)), "0"(_b)); \
	__b; \
})


static inline int64_t
cf_atomic64_addunless(cf_atomic64 *a, int64_t b, int64_t x)
{
	int64_t prior, cur;

	/* Get the current value of the atomic integer */
	cur = cf_atomic64_get(*a);

	for ( ;; ) {
		/* Check if the current value is equal to the criterion */
		if (cur == b)
			break;

		/* Attempt a compare-and-swap, which will return the value of cur
		 * prior to the operation */
		prior = cf_atomic64_cas(a, cur, cur + x);

		/* If prior and cur are equal, then the operation has succeeded;
		 * otherwise, set cur to prior and go around again */
		if (prior == cur)
			break;
		cur = prior;
	}

	return(cur != b);
}


static inline int64_t
cf_atomic64_setmax(cf_atomic64 *a, int64_t x)
{
	int64_t prior, cur;

	/* Get the current value of the atomic integer */
	cur = cf_atomic64_get(*a);

	for ( ;; ) {
		/* Check if the current value is equal to the criterion */
		if (cur >= x)
			break;

		/* Attempt a compare-and-swap, which will return the value of cur
		 * prior to the operation */
		prior = cf_atomic64_cas(a, cur, x);

		/* If prior and cur are equal, then the operation has succeeded;
		 * otherwise, set cur to prior and go around again */
		if (prior == cur)
			break;
		cur = prior;
	}

	return(cur != x);
}

#endif // uint64

#ifdef HAS_ATOMIC_32


#define cf_atomic32_get(a) (a)
#define cf_atomic32_set(a, b) (*(a) = (b))

static inline int64_t
cf_atomic32_add(cf_atomic32 *a, int32_t b)
{
	int32_t i = b;

	__asm__ __volatile__ ("lock; xadd %0, %1" : "+r" (b), "+m" (*a) : : "memory");

	return(b + i);
}
#define cf_atomic32_sub(a,b) (cf_atomic32_add((a), (0 - (b))))
#define cf_atomic32_incr(a) (cf_atomic32_add((a), 1))
#define cf_atomic32_decr(a) (cf_atomic32_add((a), -1))

static inline int32_t
cf_atomic32_cas(cf_atomic32 *a, int32_t b, int32_t x)
{
	int32_t p;

	__asm__ __volatile__ ("lock; cmpxchg %1,%2" : "=a"(p) : "q"(x), "m"(*(a)), "0"(b) : "memory");

	return(p);
}

#define cf_atomic32_cas_m(_a, _b, _x) \
({  __typeof__(_b) __b = _b; \
	__asm__ __volatile__ ("lock; cmpxchg %1,%2" : "=a"(__b) : "q"(_x), "m"(*(_a)), "0"(_b) : "memory"); \
	__b; \
})

/* cf_atomic32_fas
 * Fetch-and-swap: swap the values of  b and a */
static inline int32_t
cf_atomic32_fas(cf_atomic32 *a, cf_atomic32 *b)
{
	int32_t p;

	__asm__ __volatile__ ("lock; xchg %0,%1" : "=r"(p) : "m"(*(a)), "0"(*(b)) : "memory");

	return(p);
}


#define cf_atomic32_fas_m(_a, _b) \
({  __typeof__(_b) __b; \
	__asm__ __volatile__ ("lock; xchg %0,%1" : "=r"(__b) : "m"(*(_a)), "0"(_b)); \
	__b; \
})


static inline int32_t
cf_atomic32_addunless(cf_atomic32 *a, int32_t b, int32_t x)
{
	int32_t prior, cur;

	/* Get the current value of the atomic integer */
	cur = cf_atomic32_get(*a);

	for ( ;; ) {
		/* Check if the current value is equal to the criterion */
		if (cur == b)
			break;

		/* Attempt a compare-and-swap, which will return the value of cur
		 * prior to the operation */
		prior = cf_atomic32_cas(a, cur, cur + x);

		/* If prior and cur are equal, then the operation has succeeded;
		 * otherwise, set cur to prior and go around again */
		if (prior == cur)
			break;
		cur = prior;
	}

	return(cur != b);
}


static inline int32_t
cf_atomic32_setmax(cf_atomic32 *a, int32_t x)
{
	int32_t prior, cur;

	/* Get the current value of the atomic integer */
	cur = cf_atomic32_get(*a);

	for ( ;; ) {
		/* Check if the current value is equal to the criterion */
		if (cur >= x)
			break;

		/* Attempt a compare-and-swap, which will return the value of cur
		 * prior to the operation */
		prior = cf_atomic32_cas(a, cur, x);

		/* If prior and cur are equal, then the operation has succeeded;
		 * otherwise, set cur to prior and go around again */
		if (prior == cur)
			break;
		cur = prior;
	}

	return(cur != x);
}

#endif // uint32

#ifdef MARCH_i686

#define cf_atomic_p_get(_a) cf_atomic32_get(_a)
#define cf_atomic_p_set(_a, _b) cf_atomic32_set(_a, _b)
#define cf_atomic_p_add(_a, _b) cf_atomic32_add(_a, _b)
#define cf_atomic_p_incr(_a) cf_atomic32_add((_a), 1)
#define cf_atomic_p_decr(_a) cf_atomic32_add((_a), -1)
#define cf_atomic_p_cas(_a, _b, _x) cf_atomic32_cas(_a, _b, _x)
#define cf_atomic_p_cas_m(_a, _b, _x) cf_atomic32_cas_m(_a, _b, _x)
#define cf_atomic_p_fas(_a, _b) cf_atomic32_fas(_a, _b)
#define cf_atomic_p_fas_m(_a, _b) cf_atomic32_fas_m(_a, _b)
#define cf_atomic_p_addunless(_a, _b, _x) cf_atomic32_addunless(_a, _b, _x)
#define cf_atomic_p_setmax(_a, _x) cf_atomic32_setmax(_a, _x)

#define cf_atomic_int_get(_a) cf_atomic32_get(_a)
#define cf_atomic_int_set(_a, _b) cf_atomic32_set(_a, _b)
#define cf_atomic_int_add(_a, _b) cf_atomic32_add(_a, _b)
#define cf_atomic_int_sub(_a, _b) cf_atomic32_sub(_a, _b)
#define cf_atomic_int_incr(_a) cf_atomic32_add((_a), 1)
#define cf_atomic_int_decr(_a) cf_atomic32_add((_a), -1)
#define cf_atomic_int_cas(_a, _b, _x) cf_atomic32_cas(_a, _b, _x)
#define cf_atomic_int_cas_m(_a, _b, _x) cf_atomic32_cas_m(_a, _b, _x)
#define cf_atomic_int_fas(_a, _b) cf_atomic32_fas(_a, _b)
#define cf_atomic_int_fas_m(_a, _b) cf_atomic32_fas_m(_a, _b)
#define cf_atomic_int_addunless(_a, _b, _x) cf_atomic32_addunless(_a, _b, _x)
#define cf_atomic_int_setmax(_a, _x) cf_atomic32_setmax(_a, _x)

#endif



#ifdef MARCH_x86_64

#define cf_atomic_p_get(_a) cf_atomic64_get(_a)
#define cf_atomic_p_set(_a, _b) cf_atomic64_set(_a, _b)
#define cf_atomic_p_add(_a, _b) cf_atomic64_add(_a, _b)
#define cf_atomic_p_incr(_a) cf_atomic64_add((_a), 1)
#define cf_atomic_p_decr(_a) cf_atomic64_add((_a), -1)
#define cf_atomic_p_cas(_a, _b, _x) cf_atomic64_cas(_a, _b, _x)
#define cf_atomic_p_cas_m(_a, _b, _x) cf_atomic64_cas_m(_a, _b, _x)
#define cf_atomic_p_fas(_a, _b) cf_atomic64_fas(_a, _b)
#define cf_atomic_p_fas_m(_a, _b) cf_atomic64_fas_m(_a, _b)
#define cf_atomic_p_addunless(_a, _b, _x) cf_atomic64_addunless(_a, _b, _x)
#define cf_atomic_p_setmax(_a, _x) cf_atomic64_setmax(_a, _x)

#define cf_atomic_int_get(_a) cf_atomic64_get(_a)
#define cf_atomic_int_set(_a, _b) cf_atomic64_set(_a, _b)
#define cf_atomic_int_add(_a, _b) cf_atomic64_add(_a, _b)
#define cf_atomic_int_sub(_a, _b) cf_atomic64_sub(_a, _b)
#define cf_atomic_int_incr(_a) cf_atomic64_add((_a), 1)
#define cf_atomic_int_decr(_a) cf_atomic64_add((_a), -1)
#define cf_atomic_int_cas(_a, _b, _x) cf_atomic64_cas(_a, _b, _x)
#define cf_atomic_int_cas_m(_a, _b, _x) cf_atomic64_cas_m(_a, _b, _x)
#define cf_atomic_int_fas(_a, _b) cf_atomic64_fas(_a, _b)
#define cf_atomic_int_fas_m(_a, _b) cf_atomic64_fas_m(_a, _b)
#define cf_atomic_int_addunless(_a, _b, _x) cf_atomic64_addunless(_a, _b, _x)
#define cf_atomic_int_setmax(_a, _x) cf_atomic64_setmax(_a, _x)

#endif


/* Memory barriers */

// Knowledge taken from Linux's atomic_ops.h:

#define smb_mb() asm volatile("mfence":::"memory")


/* CF_BARRIER
 * All preceding memory accesses must commit before any following accesses */
#define CF_MEMORY_BARRIER() __asm__ __volatile__ ("lock; addl $0,0(%%esp)" : : : "memory")

/* CF_BARRIER_READ
 * All preceding memory accesses must commit before any following accesses */
#define CF_MEMORY_BARRIER_READ() CF_MEMORY_BARRIER()

/* CF_BARRIER_WRITE
 * All preceding memory accesses must commit before any following accesses */
#define CF_MEMORY_BARRIER_WRITE() __asm__ __volatile__ ("" : : : "memory")
