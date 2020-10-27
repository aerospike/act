/*
 * cgf_index.h
 *
 * Copyright (c) 2018-2020 Aerospike, Inc. All rights reserved.
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

#include <stdbool.h>
#include <stdint.h>

#include "common/cfg.h"


//==========================================================
// Typedefs & constants.
//

#define MAX_NUM_INDEX_DEVICES 16

typedef struct index_cfg_s {
	char device_names[MAX_NUM_INDEX_DEVICES][MAX_DEVICE_NAME_SIZE];
	uint32_t num_devices;           // derived by counting device names
	uint64_t file_size;             // undocumented feature - use files
	uint32_t service_threads;
	uint32_t cache_threads;
	uint64_t run_us;                // converted from literal units in seconds
	uint64_t report_interval_us;    // converted from literal units in seconds
	bool us_histograms;
	uint32_t read_reqs_per_sec;
	uint32_t write_reqs_per_sec;
	uint32_t replication_factor;
	uint32_t defrag_lwm_pct;
	bool disable_odsync;
	uint64_t max_lag_usec;          // converted from literal units in seconds
	const char* scheduler_mode;

	// Derived from literal configuration:
	uint64_t service_thread_reads_per_sec;
	uint64_t cache_thread_reads_and_writes_per_sec;
} index_cfg;


//==========================================================
// Globals.
//

extern index_cfg g_icfg;


//==========================================================
// Public API.
//

bool index_configure(int argc, char* argv[]);
