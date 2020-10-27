/*
 * random.c
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
// Typedefs & constants.
//

#define INTERVAL_SIZE 512
#define WRITES_PER_INTERVAL (INTERVAL_SIZE / sizeof(uint64_t))


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
// Fill a buffer with random bits. For buffers
// larger than INTERVAL_SIZE, rand_pct specifies
// how much to randomize. The rest is zeroed.
//
void
rand_fill(uint8_t* p_buffer, uint32_t size, uint32_t rand_pct)
{
	uint64_t* p_write = (uint64_t*)p_buffer;
	uint64_t* p_end = (uint64_t*)(p_buffer + size);
	// ... relies on size being a multiple of 8, which it will be.

	if (rand_pct < 100) {
		// Split writes per interval as specified by rand_pct. (Calculate
		// n_zeros first so rand_pct = 1 yields n_rands = 1 instead of 0.)
		uint32_t n_zeros = (WRITES_PER_INTERVAL * (100 - rand_pct)) / 100;
		uint32_t n_rands = WRITES_PER_INTERVAL - n_zeros;

		for (uint32_t i = size / INTERVAL_SIZE; i != 0; i--) {
			for (uint32_t z = n_zeros; z != 0; z--) {
				*p_write++ = 0;
			}

			for (uint32_t r = n_rands; r != 0; r--) {
				*p_write++ = xorshift128plus();
			}
		}
	}

	while (p_write < p_end) {
		*p_write++ = xorshift128plus();
	}
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
