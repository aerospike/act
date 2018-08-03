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


//==========================================================
// Forward declarations.
//

static inline uint64_t rand_64();
static inline uint64_t xorshift128plus(uint64_t* p0, uint64_t* p1);


//==========================================================
// Public API.
//

//------------------------------------------------
// Seed for random fill.
//
bool
rand_seed()
{
	fprintf(stdout, "Using xorshift+ random generator.\n\n");
	// Assumes parent program has called srand(), which is all we need here.
	return true;
}

//------------------------------------------------
// Fill a buffer with random bits.
//
bool
rand_fill(uint8_t* p_buffer, uint32_t size)
{
	uint64_t seed0 = rand_64();
	uint64_t seed1 = rand_64();

	uint64_t* p_write = (uint64_t*)p_buffer;
	uint64_t* p_end = (uint64_t*)(p_buffer + size);
	// ... relies on size being a multiple of 8, which it will be.

	while (p_write < p_end) {
		*p_write++ = xorshift128plus(&seed0, &seed1);
	}

	return true;
}


//==========================================================
// Local helpers.
//

//------------------------------------------------
// Get a mostly random uint64_t.
//
static inline uint64_t
rand_64()
{
	// Doesn't need to be perfect, just not 0 and not the same as last time.
	return ((uint64_t)rand() << 32) | (uint64_t)rand();
}

//------------------------------------------------
// One step in generating a random sequence.
//
static inline uint64_t
xorshift128plus(uint64_t* p0, uint64_t* p1)
{
	uint64_t s1 = *p0;
	uint64_t s0 = *p1;

	*p0 = s0;
	s1 ^= s1 << 23;

	return (*p1 = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))) + s0;
}
