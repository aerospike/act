/*
 * cfg_index.c
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

//==========================================================
// Includes.
//

#include "cfg_index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/cfg.h"
#include "common/hardware.h"
#include "common/trace.h"


//==========================================================
// Typedefs & constants.
//

static const char TAG_DEVICE_NAMES[]            = "device-names";
static const char TAG_FILE_SIZE_MBYTES[]        = "file-size-mbytes";
static const char TAG_SERVICE_THREADS[]         = "service-threads";
static const char TAG_CACHE_THREADS[]           = "cache-threads";
static const char TAG_TEST_DURATION_SEC[]       = "test-duration-sec";
static const char TAG_REPORT_INTERVAL_SEC[]     = "report-interval-sec";
static const char TAG_MICROSECOND_HISTOGRAMS[]  = "microsecond-histograms";
static const char TAG_READ_REQS_PER_SEC[]       = "read-reqs-per-sec";
static const char TAG_WRITE_REQS_PER_SEC[]      = "write-reqs-per-sec";
static const char TAG_REPLICATION_FACTOR[]      = "replication-factor";
static const char TAG_DEFRAG_LWM_PCT[]          = "defrag-lwm-pct";
static const char TAG_DISABLE_ODSYNC[]          = "disable-odsync";
static const char TAG_MAX_LAG_SEC[]             = "max-lag-sec";
static const char TAG_SCHEDULER_MODE[]          = "scheduler-mode";


//==========================================================
// Forward declarations.
//

static bool check_configuration();
static bool derive_configuration();
static void echo_configuration();


//==========================================================
// Globals.
//

// Configuration instance, showing non-zero defaults.
index_cfg g_icfg = {
		.cache_threads = 8,
		.report_interval_us = 1000000,
		.replication_factor = 1,
		.defrag_lwm_pct = 50,
		.max_lag_usec = 1000000 * 10,
		.scheduler_mode = "noop"
};


//==========================================================
// Public API.
//

bool
index_configure(int argc, char* argv[])
{
	if (argc != 2) {
		printf("usage: act_index [config filename]\n");
		return false;
	}

	FILE* config_file = fopen(argv[1], "r");

	if (config_file == NULL) {
		printf("ERROR: couldn't open config file %s errno %d '%s'\n", argv[1],
				errno, act_strerror(errno));
		return false;
	}

	char line[1024];

	while (fgets(line, sizeof(line), config_file) != NULL) {
		char* comment = strchr(line, '#');

		if (comment != NULL) {
			*comment = '\0';
		}

		const char* tag = strtok(line, ":" WHITE_SPACE);

		if (tag == NULL) {
			continue;
		}

		if (strcmp(tag, TAG_DEVICE_NAMES) == 0) {
			parse_device_names(MAX_NUM_INDEX_DEVICES, g_icfg.device_names,
					&g_icfg.num_devices);
		}
		else if (strcmp(tag, TAG_FILE_SIZE_MBYTES) == 0) {
			g_icfg.file_size = (uint64_t)parse_uint32() << 20;
		}
		else if (strcmp(tag, TAG_SERVICE_THREADS) == 0) {
			g_icfg.service_threads = parse_uint32();
		}
		else if (strcmp(tag, TAG_CACHE_THREADS) == 0) {
			g_icfg.cache_threads = parse_uint32();
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
		else if (strcmp(tag, TAG_DISABLE_ODSYNC) == 0) {
			g_icfg.disable_odsync = parse_yes_no();
		}
		else if (strcmp(tag, TAG_MAX_LAG_SEC) == 0) {
			g_icfg.max_lag_usec = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_SCHEDULER_MODE) == 0) {
			g_icfg.scheduler_mode = parse_scheduler_mode();
		}
		else {
			printf("ERROR: ignoring unknown config item '%s'\n", tag);
			return false;
		}
	}

	fclose(config_file);

	if (! check_configuration() || ! derive_configuration()) {
		return false;
	}

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

	if (g_icfg.service_threads == 0 &&
			(g_icfg.service_threads = 5 * num_cpus()) == 0) {
		configuration_error(TAG_SERVICE_THREADS);
		return false;
	}

	if (g_icfg.cache_threads == 0) {
		configuration_error(TAG_CACHE_THREADS);
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

static bool
derive_configuration()
{
	if (g_icfg.read_reqs_per_sec + g_icfg.write_reqs_per_sec == 0) {
		printf("ERROR: %s and %s can't both be zero\n", TAG_READ_REQS_PER_SEC,
				TAG_WRITE_REQS_PER_SEC);
		return false;
	}

	// 'replication-factor' > 1 causes replica writes.
	uint32_t effective_write_reqs_per_sec =
			g_icfg.replication_factor * g_icfg.write_reqs_per_sec;

	// On the service threads, we'll have 1 4K device read per read request, and
	// 1 4K device read per write request (including replica writes).
	g_icfg.service_thread_reads_per_sec =
			g_icfg.read_reqs_per_sec + effective_write_reqs_per_sec;

	// Load must be enough to calculate service thread rates safely.
	if (g_icfg.service_thread_reads_per_sec / g_icfg.service_threads == 0) {
		printf("ERROR: load config too small\n");
		return false;
	}

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

	return true;
}

static void
echo_configuration()
{
	printf("ACT-INDEX CONFIGURATION\n");

	printf("%s:", TAG_DEVICE_NAMES);

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		printf(" %s", g_icfg.device_names[d]);
	}

	printf("\nnum-devices: %" PRIu32 "\n", g_icfg.num_devices);

	if (g_icfg.file_size != 0) { // undocumented - don't always expose
		printf("%s: %" PRIu64 "\n", TAG_FILE_SIZE_MBYTES,
				g_icfg.file_size >> 20);
	}

	printf("%s: %" PRIu32 "\n", TAG_SERVICE_THREADS,
			g_icfg.service_threads);
	printf("%s: %" PRIu32 "\n", TAG_CACHE_THREADS,
			g_icfg.cache_threads);
	printf("%s: %" PRIu64 "\n", TAG_TEST_DURATION_SEC,
			g_icfg.run_us / 1000000);
	printf("%s: %" PRIu64 "\n", TAG_REPORT_INTERVAL_SEC,
			g_icfg.report_interval_us / 1000000);
	printf("%s: %s\n", TAG_MICROSECOND_HISTOGRAMS,
			g_icfg.us_histograms ? "yes" : "no");
	printf("%s: %" PRIu32 "\n", TAG_READ_REQS_PER_SEC,
			g_icfg.read_reqs_per_sec);
	printf("%s: %" PRIu32 "\n", TAG_WRITE_REQS_PER_SEC,
			g_icfg.write_reqs_per_sec);
	printf("%s: %" PRIu32 "\n", TAG_REPLICATION_FACTOR,
			g_icfg.replication_factor);
	printf("%s: %" PRIu32 "\n", TAG_DEFRAG_LWM_PCT,
			g_icfg.defrag_lwm_pct);
	printf("%s: %s\n", TAG_DISABLE_ODSYNC,
			g_icfg.disable_odsync ? "yes" : "no");
	printf("%s: %" PRIu64 "\n", TAG_MAX_LAG_SEC,
			g_icfg.max_lag_usec / 1000000);
	printf("%s: %s\n", TAG_SCHEDULER_MODE,
			g_icfg.scheduler_mode);

	printf("\nDERIVED CONFIGURATION\n");

	printf("service-thread-reads-per-sec: %" PRIu64 "\n",
			g_icfg.service_thread_reads_per_sec);
	printf("cache-thread-reads-and-writes-per-sec: %" PRIu64 "\n",
			g_icfg.cache_thread_reads_and_writes_per_sec);

	printf("\n");
}
