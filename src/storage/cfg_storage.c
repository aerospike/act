/*
 * cfg_storage.c
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

#include "cfg_storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "common/cfg.h"
#include "common/hardware.h"


//==========================================================
// Typedefs & constants.
//

static const char TAG_DEVICE_NAMES[]            = "device-names";
static const char TAG_NUM_QUEUES[]              = "num-queues";
static const char TAG_THREADS_PER_QUEUE[]       = "threads-per-queue";
static const char TAG_TEST_DURATION_SEC[]       = "test-duration-sec";
static const char TAG_REPORT_INTERVAL_SEC[]     = "report-interval-sec";
static const char TAG_MICROSECOND_HISTOGRAMS[]  = "microsecond-histograms";
static const char TAG_READ_REQS_PER_SEC[]       = "read-reqs-per-sec";
static const char TAG_WRITE_REQS_PER_SEC[]      = "write-reqs-per-sec";
static const char TAG_RECORD_BYTES[]            = "record-bytes";
static const char TAG_RECORD_BYTES_RANGE_MAX[]  = "record-bytes-range-max";
static const char TAG_LARGE_BLOCK_OP_KBYTES[]   = "large-block-op-kbytes";
static const char TAG_REPLICATION_FACTOR[]      = "replication-factor";
static const char TAG_UPDATE_PCT[]              = "update-pct";
static const char TAG_DEFRAG_LWM_PCT[]          = "defrag-lwm-pct";
static const char TAG_COMMIT_TO_DEVICE[]        = "commit-to-device";
static const char TAG_COMMIT_MIN_BYTES[]        = "commit-min-bytes";
static const char TAG_TOMB_RAIDER[]             = "tomb-raider";
static const char TAG_TOMB_RAIDER_SLEEP_USEC[]  = "tomb-raider-sleep-usec";
static const char TAG_MAX_REQS_QUEUED[]         = "max-reqs-queued";
static const char TAG_MAX_LAG_SEC[]             = "max-lag-sec";
static const char TAG_SCHEDULER_MODE[]          = "scheduler-mode";

#define RBLOCK_SIZE 128 // must be power of 2


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
storage_cfg g_scfg = {
		.threads_per_queue = 4,
		.replication_factor = 1,
		.defrag_lwm_pct = 50,
		.max_reqs_queued = 100000,
		.max_lag_usec = 1000000 * 10,
		.scheduler_mode = "noop"
};


//==========================================================
// Inlines & macros.
//

static inline bool
is_power_of_2(uint32_t value)
{
	return (value & (value - 1)) == 0;
}

static inline uint32_t
round_up_to_rblock(uint32_t size)
{
	return (size + (RBLOCK_SIZE - 1)) & -RBLOCK_SIZE;
}


//==========================================================
// Public API.
//

bool
storage_configure(int argc, char* argv[])
{
	if (argc != 2) {
		fprintf(stdout, "usage: act_storage [config filename]\n");
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
		char* comment = strchr(line, '#');

		if (comment) {
			*comment = '\0';
		}

		const char* tag = strtok(line, ":" WHITE_SPACE);

		if (! tag) {
			continue;
		}

		if (strcmp(tag, TAG_DEVICE_NAMES) == 0) {
			parse_device_names(MAX_NUM_STORAGE_DEVICES, g_scfg.device_names,
					&g_scfg.num_devices);
		}
		else if (strcmp(tag, TAG_NUM_QUEUES) == 0) {
			g_scfg.num_queues = parse_uint32();
		}
		else if (strcmp(tag, TAG_THREADS_PER_QUEUE) == 0) {
			g_scfg.threads_per_queue = parse_uint32();
		}
		else if (strcmp(tag, TAG_TEST_DURATION_SEC) == 0) {
			g_scfg.run_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_REPORT_INTERVAL_SEC) == 0) {
			g_scfg.report_interval_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_MICROSECOND_HISTOGRAMS) == 0) {
			g_scfg.us_histograms = parse_yes_no();
		}
		else if (strcmp(tag, TAG_READ_REQS_PER_SEC) == 0) {
			g_scfg.read_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_WRITE_REQS_PER_SEC) == 0) {
			g_scfg.write_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_RECORD_BYTES) == 0) {
			g_scfg.record_bytes = parse_uint32();
		}
		else if (strcmp(tag, TAG_RECORD_BYTES_RANGE_MAX) == 0) {
			g_scfg.record_bytes_rmx = parse_uint32();
		}
		else if (strcmp(tag, TAG_LARGE_BLOCK_OP_KBYTES) == 0) {
			g_scfg.large_block_ops_bytes = parse_uint32() * 1024;
		}
		else if (strcmp(tag, TAG_REPLICATION_FACTOR) == 0) {
			g_scfg.replication_factor = parse_uint32();
		}
		else if (strcmp(tag, TAG_UPDATE_PCT) == 0) {
			g_scfg.update_pct = parse_uint32();
		}
		else if (strcmp(tag, TAG_DEFRAG_LWM_PCT) == 0) {
			g_scfg.defrag_lwm_pct = parse_uint32();
		}
		else if (strcmp(tag, TAG_COMMIT_TO_DEVICE) == 0) {
			g_scfg.commit_to_device = parse_yes_no();
		}
		else if (strcmp(tag, TAG_COMMIT_MIN_BYTES) == 0) {
			g_scfg.commit_min_bytes = parse_uint32();
		}
		else if (strcmp(tag, TAG_TOMB_RAIDER) == 0) {
			g_scfg.tomb_raider = parse_yes_no();
		}
		else if (strcmp(tag, TAG_TOMB_RAIDER_SLEEP_USEC) == 0) {
			g_scfg.tomb_raider_sleep_us = parse_uint32();
		}
		else if (strcmp(tag, TAG_MAX_REQS_QUEUED) == 0) {
			g_scfg.max_reqs_queued = parse_uint32();
		}
		else if (strcmp(tag, TAG_MAX_LAG_SEC) == 0) {
			g_scfg.max_lag_usec = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_SCHEDULER_MODE) == 0) {
			g_scfg.scheduler_mode = parse_scheduler_mode();
		}
		else {
			fprintf(stdout, "ERROR: ignoring unknown config item '%s'\n", tag);
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
	if (g_scfg.num_devices == 0) {
		configuration_error(TAG_DEVICE_NAMES);
		return false;
	}

	if (g_scfg.num_queues == 0 && (g_scfg.num_queues = num_cpus()) == 0) {
		configuration_error(TAG_NUM_QUEUES);
		return false;
	}

	if (g_scfg.threads_per_queue == 0) {
		configuration_error(TAG_THREADS_PER_QUEUE);
		return false;
	}

	if (g_scfg.run_us == 0) {
		configuration_error(TAG_TEST_DURATION_SEC);
		return false;
	}

	if (g_scfg.report_interval_us == 0) {
		configuration_error(TAG_REPORT_INTERVAL_SEC);
		return false;
	}

	if (g_scfg.read_reqs_per_sec == 0) {
		configuration_error(TAG_READ_REQS_PER_SEC);
		return false;
	}

	if (g_scfg.record_bytes == 0) {
		configuration_error(TAG_RECORD_BYTES);
		return false;
	}

	if (g_scfg.record_bytes_rmx != 0 &&
			g_scfg.record_bytes_rmx <= g_scfg.record_bytes) {
		configuration_error(TAG_RECORD_BYTES_RANGE_MAX);
		return false;
	}

	if (g_scfg.large_block_ops_bytes < g_scfg.record_bytes ||
			g_scfg.large_block_ops_bytes < g_scfg.record_bytes_rmx ||
			! is_power_of_2(g_scfg.large_block_ops_bytes)) {
		configuration_error(TAG_LARGE_BLOCK_OP_KBYTES);
		return false;
	}

	if (g_scfg.replication_factor == 0) {
		configuration_error(TAG_REPLICATION_FACTOR);
		return false;
	}

	if (g_scfg.update_pct > 100) {
		configuration_error(TAG_UPDATE_PCT);
		return false;
	}

	if (g_scfg.defrag_lwm_pct >= 100) {
		configuration_error(TAG_DEFRAG_LWM_PCT);
		return false;
	}

	if (g_scfg.commit_min_bytes != 0 &&
			(g_scfg.commit_min_bytes > g_scfg.large_block_ops_bytes ||
			! is_power_of_2(g_scfg.commit_min_bytes))) {
		configuration_error(TAG_COMMIT_MIN_BYTES);
		return false;
	}

	return true;
}

static void
derive_configuration()
{
	// Non-zero update-pct causes client writes to generate internal reads.
	g_scfg.internal_read_reqs_per_sec = g_scfg.read_reqs_per_sec +
			(g_scfg.write_reqs_per_sec * g_scfg.update_pct / 100);

	// 'replication-factor' > 1 causes replica writes (which are replaces).
	uint32_t internal_write_reqs_per_sec =
			g_scfg.replication_factor * g_scfg.write_reqs_per_sec;

	g_scfg.record_stored_bytes = round_up_to_rblock(g_scfg.record_bytes);

	g_scfg.record_stored_bytes_rmx = g_scfg.record_bytes_rmx == 0 ?
			g_scfg.record_stored_bytes :
			round_up_to_rblock(g_scfg.record_bytes_rmx);

	// Assumes linear probability distribution across size range.
	uint32_t avg_record_stored_bytes =
			(g_scfg.record_stored_bytes + g_scfg.record_stored_bytes_rmx) / 2;

	// "Original" means excluding write rate due to defrag.
	double original_write_rate_in_large_blocks_per_sec =
			(double)internal_write_reqs_per_sec /
			(double)(g_scfg.large_block_ops_bytes / avg_record_stored_bytes);

	double defrag_write_amplification =
			100.0 / (double)(100 - g_scfg.defrag_lwm_pct);
	// For example:
	// defrag-lwm-pct = 50: amplification = 100/(100 - 50) = 2.0 (default)
	// defrag-lwm-pct = 60: amplification = 100/(100 - 60) = 2.5
	// defrag-lwm-pct = 40: amplification = 100/(100 - 40) = 1.666...

	// Large block read rate always matches overall write rate.
	g_scfg.large_block_reads_per_sec =
			original_write_rate_in_large_blocks_per_sec *
			defrag_write_amplification;

	if (g_scfg.commit_to_device) {
		// In 'commit-to-device' mode, only write rate caused by defrag is done
		// via large block writes.
		g_scfg.large_block_writes_per_sec =
				original_write_rate_in_large_blocks_per_sec *
				(defrag_write_amplification - 1.0);

		// "Original" writes are done individually.
		g_scfg.internal_write_reqs_per_sec = internal_write_reqs_per_sec;
	}
	else {
		// Normally, overall write rate is all done via large block writes.
		g_scfg.large_block_writes_per_sec = g_scfg.large_block_reads_per_sec;
	}
}

static void
echo_configuration()
{
	fprintf(stdout, "ACT_STORAGE CONFIGURATION\n");

	fprintf(stdout, "%s:", TAG_DEVICE_NAMES);

	for (int d = 0; d < g_scfg.num_devices; d++) {
		fprintf(stdout, " %s", g_scfg.device_names[d]);
	}

	fprintf(stdout, "\nnum-devices: %" PRIu32 "\n",
			g_scfg.num_devices);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_NUM_QUEUES,
			g_scfg.num_queues);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_THREADS_PER_QUEUE,
			g_scfg.threads_per_queue);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_TEST_DURATION_SEC,
			g_scfg.run_us / 1000000);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_REPORT_INTERVAL_SEC,
			g_scfg.report_interval_us / 1000000);
	fprintf(stdout, "%s: %s\n", TAG_MICROSECOND_HISTOGRAMS,
			g_scfg.us_histograms ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_READ_REQS_PER_SEC,
			g_scfg.read_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_WRITE_REQS_PER_SEC,
			g_scfg.write_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_RECORD_BYTES,
			g_scfg.record_bytes);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_RECORD_BYTES_RANGE_MAX,
			g_scfg.record_bytes_rmx);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_LARGE_BLOCK_OP_KBYTES,
			g_scfg.large_block_ops_bytes / 1024);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_REPLICATION_FACTOR,
			g_scfg.replication_factor);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_UPDATE_PCT,
			g_scfg.update_pct);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_DEFRAG_LWM_PCT,
			g_scfg.defrag_lwm_pct);
	fprintf(stdout, "%s: %s\n", TAG_COMMIT_TO_DEVICE,
			g_scfg.commit_to_device ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_COMMIT_MIN_BYTES,
			g_scfg.commit_min_bytes);
	fprintf(stdout, "%s: %s\n", TAG_TOMB_RAIDER,
			g_scfg.tomb_raider ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_TOMB_RAIDER_SLEEP_USEC,
			g_scfg.tomb_raider_sleep_us);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_MAX_REQS_QUEUED,
			g_scfg.max_reqs_queued);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_MAX_LAG_SEC,
			g_scfg.max_lag_usec / 1000000);
	fprintf(stdout, "%s: %s\n", TAG_SCHEDULER_MODE,
			g_scfg.scheduler_mode);

	fprintf(stdout, "\n");

	fprintf(stdout, "internal read requests per sec: %" PRIu64 "\n",
			g_scfg.internal_read_reqs_per_sec);
	fprintf(stdout, "internal write requests per sec: %" PRIu64 "\n",
			g_scfg.internal_write_reqs_per_sec);
	fprintf(stdout, "bytes per stored record: %" PRIu32 " ... %" PRIu32 "\n",
			g_scfg.record_stored_bytes, g_scfg.record_stored_bytes_rmx);
	fprintf(stdout, "large block reads per sec: %.2lf\n",
			g_scfg.large_block_reads_per_sec);
	fprintf(stdout, "large block writes per sec: %.2lf\n",
			g_scfg.large_block_writes_per_sec);

	fprintf(stdout, "\n");
}
