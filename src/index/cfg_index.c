/*
 * cfg_index.c
 *
 * Copyright (c) 2018 Aerospike, Inc. All rights reserved.
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

#include "cfg_index.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "common/cfg.h"


//==========================================================
// Typedefs & constants.
//

static const char TAG_DEVICE_NAMES[]            = "device-names";
static const char TAG_NUM_QUEUES[]              = "num-queues";
static const char TAG_THREADS_PER_QUEUE[]       = "threads-per-queue";
static const char TAG_NUM_CACHE_THREADS[]       = "num-cache-threads";
static const char TAG_TEST_DURATION_SEC[]       = "test-duration-sec";
static const char TAG_REPORT_INTERVAL_SEC[]     = "report-interval-sec";
static const char TAG_MICROSECOND_HISTOGRAMS[]  = "microsecond-histograms";
static const char TAG_READ_REQS_PER_SEC[]       = "read-reqs-per-sec";
static const char TAG_WRITE_REQS_PER_SEC[]      = "write-reqs-per-sec";
static const char TAG_REPLICATION_FACTOR[]      = "replication-factor";
static const char TAG_DEFRAG_LWM_PCT[]          = "defrag-lwm-pct";
static const char TAG_SCHEDULER_MODE[]          = "scheduler-mode";


//==========================================================
// Forward declarations.
//

static bool check_configuration();
static void derive_configuration();
static void echo_configuration();


//==========================================================
// Globals.
//

// Configuration instance, showing non-zero defaults.
index_cfg g_icfg = {
		.replication_factor = 1,
		.defrag_lwm_pct = 50
};


//==========================================================
// Public API.
//

bool
index_configure(int argc, char* argv[])
{
	if (argc != 2) {
		fprintf(stdout, "usage: act_index [config filename]\n");
		return false;
	}

	FILE* config_file = fopen(argv[1], "r");

	if (! config_file) {
		fprintf(stdout, "ERROR: couldn't open config file %s errno %d '%s'\n",
				argv[1], errno, strerror(errno));
		return false;
	}

	char line[1024];

	while (fgets(line, sizeof(line), config_file)) {
		if (*line == '#') {
			continue;
		}

		const char* tag = strtok(line, ":" WHITE_SPACE);

		if (! tag) {
			continue;
		}

		if (strcmp(tag, TAG_DEVICE_NAMES) == 0) {
			parse_device_names(MAX_NUM_INDEX_DEVICES, g_icfg.device_names,
					&g_icfg.num_devices);
		}
		else if (strcmp(tag, TAG_NUM_QUEUES) == 0) {
			g_icfg.num_queues = parse_uint32();
		}
		else if (strcmp(tag, TAG_THREADS_PER_QUEUE) == 0) {
			g_icfg.threads_per_queue = parse_uint32();
		}
		else if (strcmp(tag, TAG_NUM_CACHE_THREADS) == 0) {
			g_icfg.num_cache_threads = parse_uint32();
		}
		else if (strcmp(tag, TAG_TEST_DURATION_SEC) == 0) {
			g_icfg.run_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_REPORT_INTERVAL_SEC) == 0) {
			g_icfg.report_interval_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_MICROSECOND_HISTOGRAMS) == 0) {
			g_icfg.us_histograms = parse_yes_no();
		}
		else if (strcmp(tag, TAG_READ_REQS_PER_SEC) == 0) {
			g_icfg.read_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_WRITE_REQS_PER_SEC) == 0) {
			g_icfg.write_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_REPLICATION_FACTOR) == 0) {
			g_icfg.replication_factor = parse_uint32();
		}
		else if (strcmp(tag, TAG_DEFRAG_LWM_PCT) == 0) {
			g_icfg.defrag_lwm_pct = parse_uint32();
		}
		else if (strcmp(tag, TAG_SCHEDULER_MODE) == 0) {
			g_icfg.scheduler_mode = parse_scheduler_mode();
		}
	}

	fclose(config_file);

	if (! check_configuration()) {
		return false;
	}

	derive_configuration();
	echo_configuration();

	return true;
}


//==========================================================
// Local helpers.
//

static bool
check_configuration()
{
	if (g_icfg.num_devices == 0) {
		configuration_error(TAG_DEVICE_NAMES);
		return false;
	}

	if (g_icfg.num_queues == 0) {
		configuration_error(TAG_NUM_QUEUES);
		return false;
	}

	if (g_icfg.threads_per_queue == 0) {
		configuration_error(TAG_THREADS_PER_QUEUE);
		return false;
	}

	if (g_icfg.num_cache_threads == 0) {
		configuration_error(TAG_NUM_CACHE_THREADS);
		return false;
	}

	if (g_icfg.run_us == 0) {
		configuration_error(TAG_TEST_DURATION_SEC);
		return false;
	}

	if (g_icfg.report_interval_us == 0) {
		configuration_error(TAG_REPORT_INTERVAL_SEC);
		return false;
	}

	if (g_icfg.replication_factor == 0) {
		configuration_error(TAG_REPLICATION_FACTOR);
		return false;
	}

	if (g_icfg.defrag_lwm_pct >= 100) {
		configuration_error(TAG_DEFRAG_LWM_PCT);
		return false;
	}

	return true;
}

static void
derive_configuration()
{
	// 'replication-factor' > 1 causes replica writes.
	uint32_t effective_write_reqs_per_sec =
			g_icfg.replication_factor * g_icfg.write_reqs_per_sec;

	// On the transaction threads, we'll have 1 4K device read per read request,
	// and 1 4K device read per write request (including replica writes).
	g_icfg.trans_thread_reads_per_sec =
			g_icfg.read_reqs_per_sec + effective_write_reqs_per_sec;

	// On the cache threads, we'll have extra 4K device reads per write request
	// due to defrag. We'll also have 1 4K device write per write request, plus
	// extras due to defrag. The total 4K device writes is equal to the extra
	// 4K device reads (really!), so just keep one number for both.
	double cache_thread_reads_and_writes_per_write =
			100.0 / (double)(100 - g_icfg.defrag_lwm_pct);
	// For example:
	// defrag-lwm-pct = 50: r/w-per-write = 100/(100 - 50) = 2.0 (default)
	// defrag-lwm-pct = 60: r/w-per-write = 100/(100 - 60) = 2.5
	// defrag-lwm-pct = 40: r/w-per-write = 100/(100 - 40) = 1.666...

	g_icfg.cache_thread_reads_and_writes_per_sec =
			effective_write_reqs_per_sec *
			cache_thread_reads_and_writes_per_write;
}

static void
echo_configuration()
{
	fprintf(stdout, "ACT_INDEX CONFIGURATION\n");

	fprintf(stdout, "%s:", TAG_DEVICE_NAMES);

	for (int d = 0; d < g_icfg.num_devices; d++) {
		fprintf(stdout, " %s", g_icfg.device_names[d]);
	}

	fprintf(stdout, "\nnum-devices: %" PRIu32 "\n",
			g_icfg.num_devices);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_NUM_QUEUES,
			g_icfg.num_queues);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_THREADS_PER_QUEUE,
			g_icfg.threads_per_queue);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_TEST_DURATION_SEC,
			g_icfg.run_us / 1000000);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_REPORT_INTERVAL_SEC,
			g_icfg.report_interval_us / 1000000);
	fprintf(stdout, "%s: %s\n", TAG_MICROSECOND_HISTOGRAMS,
			g_icfg.us_histograms ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_READ_REQS_PER_SEC,
			g_icfg.read_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_WRITE_REQS_PER_SEC,
			g_icfg.write_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_REPLICATION_FACTOR,
			g_icfg.replication_factor);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_DEFRAG_LWM_PCT,
			g_icfg.defrag_lwm_pct);
	fprintf(stdout, "%s: %s\n", TAG_SCHEDULER_MODE,
			SCHEDULER_MODES[g_icfg.scheduler_mode]);

	fprintf(stdout, "\n");

	fprintf(stdout, "trans thread reads per sec: %" PRIu64 "\n",
			g_icfg.trans_thread_reads_per_sec);
	fprintf(stdout, "cache thread reads and writes per sec: %" PRIu64 "\n",
			g_icfg.cache_thread_reads_and_writes_per_sec);

	fprintf(stdout, "\n");
}
