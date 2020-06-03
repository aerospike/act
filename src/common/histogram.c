/*
 * histogram.c
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

#include "histogram.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atomic.h"


//==========================================================
// Typedefs & constants.
//

//------------------------------------------------
// BYTE_MSB[n] returns the position of the most
// significant bit. If no bits are set (n = 0) it
// returns 0. Otherwise the positions are 1 ... 8
// from low to high, so e.g. n = 13 returns 4:
//
//		bits:		0  0  0  0  1  1  0  1
//		position:	8  7  6  5 [4] 3  2  1
//
static const char BYTE_MSB[] = {
		0, 1, 2, 2, 3, 3, 3, 3,  4, 4, 4, 4, 4, 4, 4, 4,
		5, 5, 5, 5, 5, 5, 5, 5,  5, 5, 5, 5, 5, 5, 5, 5,
		6, 6, 6, 6, 6, 6, 6, 6,  6, 6, 6, 6, 6, 6, 6, 6,
		6, 6, 6, 6, 6, 6, 6, 6,  6, 6, 6, 6, 6, 6, 6, 6,

		7, 7, 7, 7, 7, 7, 7, 7,  7, 7, 7, 7, 7, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7,  7, 7, 7, 7, 7, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7,  7, 7, 7, 7, 7, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7,  7, 7, 7, 7, 7, 7, 7, 7,

		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,

		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8
};


//==========================================================
// Forward declarations.
//

static int msb(uint64_t n);


//==========================================================
// Public API.
//

//------------------------------------------------
// Create a histogram. There's no destroy(), but
// you can just free the histogram.
//
histogram*
histogram_create(histogram_scale scale)
{
	histogram* h = malloc(sizeof(histogram));

	if (h == NULL) {
		printf("ERROR: creating histogram (malloc)\n");
		return NULL;
	}

	memset((void*)h->counts, 0, sizeof(h->counts));

	switch (scale) {
	case HIST_MILLISECONDS:
		h->time_div = 1000 * 1000;
		break;
	case HIST_MICROSECONDS:
		h->time_div = 1000;
		break;
	default:
		printf("ERROR: creating histogram (scale parameter)\n");
		free(h);
		return NULL;
	}

	return h;
}

//------------------------------------------------
// Dump a histogram to stdout.
//
// Note - DO NOT change the output format in this
// method - act_latency.py assumes this format.
//
void
histogram_dump(histogram* h, const char* tag)
{
	uint64_t counts[N_BUCKETS];
	uint32_t i = N_BUCKETS;
	uint32_t j = 0;
	uint64_t total = 0;

	for (uint32_t b = 0; b < N_BUCKETS; b++) {
		counts[b] = atomic64_get(h->counts[b]);

		if (counts[b] != 0) {
			if (i > b) {
				i = b;
			}

			j = b;
			total += counts[b];
		}
	}

	char buf[200];
	int pos = 0;
	uint32_t k = 0;

	buf[0] = '\0';

	printf("%s (%" PRIu64 " total)\n", tag, total);

	for ( ; i <= j; i++) {
		if (counts[i] == 0) { // print only non-zero columns
			continue;
		}

		pos += sprintf(buf + pos, " (%02u: %010" PRIu64 ")", i, counts[i]);

		if ((k & 3) == 3) { // maximum of 4 printed columns per line
			printf("%s\n", buf);
			pos = 0;
			buf[0] = '\0';
		}

		k++;
	}

	if (pos > 0) {
		printf("%s\n", buf);
	}
}

//------------------------------------------------
// Insert a time interval data point. The interval
// is specified in nanoseconds, and converted to
// milliseconds or microseconds as appropriate.
// Generates a histogram with either:
//
//		bucket	millisecond range
//		------	-----------------
//		0		0 to 1  (more exactly, 0.999999)
//		1		1 to 2  (more exactly, 1.999999)
//		2		2 to 4  (more exactly, 3.999999)
//		3		4 to 8  (more exactly, 7.999999)
//		4		8 to 16 (more exactly, 15.999999)
//		etc.
//
// or:
//
//		bucket	microsecond range
//		------	-----------------
//		0		0 to 1  (more exactly, 0.999)
//		1		1 to 2  (more exactly, 1.999)
//		2		2 to 4  (more exactly, 3.999)
//		3		4 to 8  (more exactly, 7.999)
//		4		8 to 16 (more exactly, 15.999)
//		etc.
//
void
histogram_insert_data_point(histogram* h, uint64_t delta_ns)
{
	uint64_t delta_t = delta_ns / h->time_div;
	int bucket = 0;

	if (delta_t != 0) {
		bucket = msb(delta_t);
	}

	atomic64_incr(&h->counts[bucket]);
}


//==========================================================
// Local helpers.
//

//------------------------------------------------
// Returns the position of the most significant
// bit of n. Positions are 1 ... 64 from low to
// high, so:
//
//		n			msb(n)
//		--------	------
//		0			0
//		1			1
//		2 ... 3		2
//		4 ... 7		3
//		8 ... 15	4
//		etc.
//
static int
msb(uint64_t n)
{
	int shift = 0;

	while (true) {
		uint64_t n_div_256 = n >> 8;

		if (n_div_256 == 0) {
			return shift + (int)BYTE_MSB[n];
		}

		n = n_div_256;
		shift += 8;
	}

	// Should never get here.
	printf("ERROR: msb calculation\n");
	return -1;
}
