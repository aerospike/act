/*
 * histogram.h
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

#include "atomic.h"


//==========================================================
// Typedefs & constants.
//

#define N_BUCKETS (1 + 64)

typedef enum {
	HIST_MILLISECONDS,
	HIST_MICROSECONDS,
	HIST_SCALE_MAX_PLUS_1
} histogram_scale;

typedef struct histogram_s {
	uint32_t time_div;
	atomic64 counts[N_BUCKETS];
} histogram;


//==========================================================
// Public API.
//

histogram* histogram_create(histogram_scale scale);
void histogram_dump(histogram* h, const char* tag);
void histogram_insert_data_point(histogram* h, uint64_t delta_ns);
