/*
 * random.c
 *
 * Copyright (c) 2008-2018 Aerospike, Inc. All rights reserved.
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

//==========================================================
// Includes.
//

#include "random.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


//==========================================================
// Forward declarations.
//

static inline uint64_t xorshift128plus();


//==========================================================
// Globals.
//

static __thread uint64_t tl_seed0;
static __thread uint64_t tl_seed1;


//==========================================================
// Public API.
//

//------------------------------------------------
// Seed for system rand() call.
//
void
rand_seed()
{
	srand(time(NULL));
}

//------------------------------------------------
// Seed a thread for generating a random sequence.
//
void
rand_seed_thread()
{
	tl_seed0 = ((uint64_t)rand() << 32) | (uint64_t)rand();
	tl_seed1 = ((uint64_t)rand() << 32) | (uint64_t)rand();
}

//------------------------------------------------
// Get a random uint32_t.
//
uint32_t
rand_32()
{
	return (uint32_t)xorshift128plus();
}

//------------------------------------------------
// Get a random uint64_t.
//
uint64_t
rand_64()
{
	return xorshift128plus();
}

//------------------------------------------------
// Fill a buffer with random bits.
//
bool
rand_fill(uint8_t* p_buffer, uint32_t size)
{
	uint64_t* p_write = (uint64_t*)p_buffer;
	uint64_t* p_end = (uint64_t*)(p_buffer + size);
	// ... relies on size being a multiple of 8, which it will be.

	while (p_write < p_end) {
		*p_write++ = xorshift128plus();
	}

	return true;
}

//------------------------------------------------
// Fill a buffer with target compression ratio.
//

bool
comp_fill(uint8_t* p_buffer, uint32_t size, uint32_t compress_percent)
{
	uint64_t* p_write = (uint64_t*)p_buffer; 	
	uint64_t* p_end = (uint64_t*)(p_buffer + size);
	// ... relies on size being a multiple of 8, which it will be.
	const uint32_t interval_size = 512;   // Space out zero runs at this interval
	const uint32_t interval_num = size / interval_size;
	const uint32_t z_len = (((100 - compress_percent) * interval_size) / 100) / sizeof(uint64_t);
	const uint32_t r_len = (interval_size / sizeof(uint64_t)) - z_len;
	// ... data must be in interval_size units to achieve target compression ratio
	int32_t i, z, r;

	for (i = 0; i < interval_num; i++) {
		z = z_len;
		while (z--) {
			*p_write++ = 0;
		}
		r = r_len;
		while (r--) {
			*p_write++ = xorshift128plus();
		}
	}

	while (p_write < p_end) {
		*p_write++ = xorshift128plus();
	}

	return true;
}



//==========================================================
// Local helpers.
//

//------------------------------------------------
// One step in generating a random sequence.
//
static inline uint64_t
xorshift128plus()
{
	uint64_t s1 = tl_seed0;
	uint64_t s0 = tl_seed1;

	tl_seed0 = s0;
	s1 ^= s1 << 23;
	tl_seed1 = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);

	return tl_seed1 + s0;
}
