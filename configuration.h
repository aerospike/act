/*
 * configuration.h
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

#pragma once

//==========================================================
// Includes.
//

#include <stdbool.h>
#include <stdint.h>


//==========================================================
// Typedefs & constants.
//

#define MAX_NUM_DEVICES 32
#define MAX_DEVICE_NAME_SIZE 64

typedef struct _act_cfg {
	char device_names[MAX_NUM_DEVICES][MAX_DEVICE_NAME_SIZE];
	uint32_t num_devices;			// derived by counting device names
	uint32_t num_queues;
	uint32_t threads_per_queue;
	uint64_t run_us;				// converted from literal units in seconds
	uint64_t report_interval_us;	// converted from literal units in seconds
	bool us_histograms;
	uint64_t read_reqs_per_sec;
	uint32_t write_reqs_per_sec;
	uint32_t record_bytes;
	uint32_t large_block_ops_bytes;	// converted from literal units in Kbytes
	uint32_t replication_factor;
	uint32_t update_pct;
	uint32_t defrag_lwm_pct;
	uint32_t scheduler_mode;		// array index derived from literal string

	// Derived from literal configuration:
	uint32_t record_stored_bytes;
	double large_block_ops_per_sec;
} act_cfg;

extern const char* const SCHEDULER_MODES[];


//==========================================================
// Globals.
//

extern act_cfg g_cfg;


//==========================================================
// Public API.
//

bool configure(int argc, char* argv[]);
