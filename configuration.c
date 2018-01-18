/*
 * configuration.c
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

#include "configuration.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


//==========================================================
// Typedefs & constants.
//

const char TAG_DEVICE_NAMES[]			= "device-names";
const char TAG_NUM_QUEUES[]				= "num-queues";
const char TAG_THREADS_PER_QUEUE[]		= "threads-per-queue";
const char TAG_TEST_DURATION_SEC[]		= "test-duration-sec";
const char TAG_REPORT_INTERVAL_SEC[]	= "report-interval-sec";
const char TAG_MICROSECOND_HISTOGRAMS[]	= "microsecond-histograms";
const char TAG_READ_REQS_PER_SEC[]		= "read-reqs-per-sec";
const char TAG_WRITE_REQS_PER_SEC[]		= "write-reqs-per-sec";
const char TAG_RECORD_BYTES[]			= "record-bytes";
const char TAG_RECORD_BYTES_RANGE_MAX[]	= "record-bytes-range-max";
const char TAG_LARGE_BLOCK_OP_KBYTES[]	= "large-block-op-kbytes";
const char TAG_REPLICATION_FACTOR[]		= "replication-factor";
const char TAG_UPDATE_PCT[]				= "update-pct";
const char TAG_DEFRAG_LWM_PCT[]			= "defrag-lwm-pct";
const char TAG_COMMIT_TO_DEVICE[]		= "commit-to-device";
const char TAG_COMMIT_MIN_BYTES[]		= "commit-min-bytes";
const char TAG_SCHEDULER_MODE[]			= "scheduler-mode";

const char* const SCHEDULER_MODES[] = {
	"noop",
	"cfq"
};

const uint32_t NUM_SCHEDULER_MODES = sizeof(SCHEDULER_MODES) / sizeof(char*);

#define WHITE_SPACE " \t\n\r"

#define RBLOCK_SIZE 128 // must be power of 2


//==========================================================
// Forward declarations.
//

static bool check_configuration();
static void derive_configuration();
static void echo_configuration();
static void parse_device_names();
static void parse_scheduler_mode();
static uint32_t parse_uint32();
static bool parse_yes_no();


//==========================================================
// Globals.
//

// Configuration instance, showing non-zero defaults.
act_cfg g_cfg = {
		.replication_factor = 1,
		.defrag_lwm_pct = 50
};


//==========================================================
// Inlines & macros.
//

static inline void
configuration_error(const char* tag)
{
	fprintf(stdout, "ERROR: invalid or missing configuration of '%s'\n", tag);
}

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
configure(int argc, char* argv[])
{
	if (argc != 2) {
		fprintf(stdout, "usage: act [config filename]\n");
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
			parse_device_names();
		}
		else if (strcmp(tag, TAG_NUM_QUEUES) == 0) {
			g_cfg.num_queues = parse_uint32();
		}
		else if (strcmp(tag, TAG_THREADS_PER_QUEUE) == 0) {
			g_cfg.threads_per_queue = parse_uint32();
		}
		else if (strcmp(tag, TAG_TEST_DURATION_SEC) == 0) {
			g_cfg.run_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_REPORT_INTERVAL_SEC) == 0) {
			g_cfg.report_interval_us = (uint64_t)parse_uint32() * 1000000;
		}
		else if (strcmp(tag, TAG_MICROSECOND_HISTOGRAMS) == 0) {
			g_cfg.us_histograms = parse_yes_no();
		}
		else if (strcmp(tag, TAG_READ_REQS_PER_SEC) == 0) {
			g_cfg.read_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_WRITE_REQS_PER_SEC) == 0) {
			g_cfg.write_reqs_per_sec = parse_uint32();
		}
		else if (strcmp(tag, TAG_RECORD_BYTES) == 0) {
			g_cfg.record_bytes = parse_uint32();
		}
		else if (strcmp(tag, TAG_RECORD_BYTES_RANGE_MAX) == 0) {
			g_cfg.record_bytes_rmx = parse_uint32();
		}
		else if (strcmp(tag, TAG_LARGE_BLOCK_OP_KBYTES) == 0) {
			g_cfg.large_block_ops_bytes = parse_uint32() * 1024;
		}
		else if (strcmp(tag, TAG_REPLICATION_FACTOR) == 0) {
			g_cfg.replication_factor = parse_uint32();
		}
		else if (strcmp(tag, TAG_UPDATE_PCT) == 0) {
			g_cfg.update_pct = parse_uint32();
		}
		else if (strcmp(tag, TAG_DEFRAG_LWM_PCT) == 0) {
			g_cfg.defrag_lwm_pct = parse_uint32();
		}
		else if (strcmp(tag, TAG_COMMIT_TO_DEVICE) == 0) {
			g_cfg.commit_to_device = parse_yes_no();
		}
		else if (strcmp(tag, TAG_COMMIT_MIN_BYTES) == 0) {
			g_cfg.commit_min_bytes = parse_uint32();
		}
		else if (strcmp(tag, TAG_SCHEDULER_MODE) == 0) {
			parse_scheduler_mode();
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
	if (g_cfg.num_devices == 0) {
		configuration_error(TAG_DEVICE_NAMES);
		return false;
	}

	if (g_cfg.num_queues == 0) {
		configuration_error(TAG_NUM_QUEUES);
		return false;
	}

	if (g_cfg.threads_per_queue == 0) {
		configuration_error(TAG_THREADS_PER_QUEUE);
		return false;
	}

	if (g_cfg.run_us == 0) {
		configuration_error(TAG_TEST_DURATION_SEC);
		return false;
	}

	if (g_cfg.report_interval_us == 0) {
		configuration_error(TAG_REPORT_INTERVAL_SEC);
		return false;
	}

	if (g_cfg.read_reqs_per_sec == 0) {
		configuration_error(TAG_READ_REQS_PER_SEC);
		return false;
	}

	if (g_cfg.record_bytes == 0) {
		configuration_error(TAG_RECORD_BYTES);
		return false;
	}

	if (g_cfg.record_bytes_rmx != 0 &&
			g_cfg.record_bytes_rmx <= g_cfg.record_bytes) {
		configuration_error(TAG_RECORD_BYTES_RANGE_MAX);
		return false;
	}

	if (g_cfg.large_block_ops_bytes < g_cfg.record_bytes ||
			g_cfg.large_block_ops_bytes < g_cfg.record_bytes_rmx ||
			! is_power_of_2(g_cfg.large_block_ops_bytes)) {
		configuration_error(TAG_LARGE_BLOCK_OP_KBYTES);
		return false;
	}

	if (g_cfg.replication_factor == 0) {
		configuration_error(TAG_REPLICATION_FACTOR);
		return false;
	}

	if (g_cfg.update_pct > 100) {
		configuration_error(TAG_UPDATE_PCT);
		return false;
	}

	if (g_cfg.defrag_lwm_pct >= 100) {
		configuration_error(TAG_DEFRAG_LWM_PCT);
		return false;
	}

	if (g_cfg.commit_min_bytes != 0 &&
			(g_cfg.commit_min_bytes > g_cfg.large_block_ops_bytes ||
			! is_power_of_2(g_cfg.commit_min_bytes))) {
		configuration_error(TAG_COMMIT_MIN_BYTES);
		return false;
	}

	return true;
}

static void
derive_configuration()
{
	// Non-zero update-pct causes client writes to generate internal reads.
	g_cfg.internal_read_reqs_per_sec = g_cfg.read_reqs_per_sec +
			(g_cfg.write_reqs_per_sec * g_cfg.update_pct / 100);

	// 'replication-factor' > 1 causes replica writes (which are replaces).
	uint32_t internal_write_reqs_per_sec =
			g_cfg.replication_factor * g_cfg.write_reqs_per_sec;

	g_cfg.record_stored_bytes = round_up_to_rblock(g_cfg.record_bytes);

	g_cfg.record_stored_bytes_rmx = g_cfg.record_bytes_rmx == 0 ?
			g_cfg.record_stored_bytes :
			round_up_to_rblock(g_cfg.record_bytes_rmx);

	// Assumes linear probability distribution across size range.
	uint32_t avg_record_stored_bytes =
			(g_cfg.record_stored_bytes + g_cfg.record_stored_bytes_rmx) / 2;

	// "Original" means excluding write rate due to defrag.
	double original_write_rate_in_large_blocks_per_sec =
			(double)internal_write_reqs_per_sec /
			(double)(g_cfg.large_block_ops_bytes / avg_record_stored_bytes);

	double defrag_write_amplification =
			100.0 / (double)(100 - g_cfg.defrag_lwm_pct);
	// For example:
	// defrag-lwm-pct = 50: amplification = 100/(100 - 50) = 2.0 (default)
	// defrag-lwm-pct = 60: amplification = 100/(100 - 60) = 2.5
	// defrag-lwm-pct = 40: amplification = 100/(100 - 40) = 1.666...

	// Large block read rate always matches overall write rate.
	g_cfg.large_block_reads_per_sec =
			original_write_rate_in_large_blocks_per_sec *
			defrag_write_amplification;

	if (g_cfg.commit_to_device) {
		// In 'commit-to-device' mode, only write rate caused by defrag is done
		// via large block writes.
		g_cfg.large_block_writes_per_sec =
				original_write_rate_in_large_blocks_per_sec *
				(defrag_write_amplification - 1.0);

		// "Original" writes are done individually.
		g_cfg.internal_write_reqs_per_sec = internal_write_reqs_per_sec;
	}
	else {
		// Normally, overall write rate is all done via large block writes.
		g_cfg.large_block_writes_per_sec = g_cfg.large_block_reads_per_sec;
	}
}

static void
echo_configuration()
{
	fprintf(stdout, "ACT CONFIGURATION\n");

	fprintf(stdout, "%s:", TAG_DEVICE_NAMES);

	for (int d = 0; d < g_cfg.num_devices; d++) {
		fprintf(stdout, " %s", g_cfg.device_names[d]);
	}

	fprintf(stdout, "\nnum-devices: %" PRIu32 "\n",
			g_cfg.num_devices);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_NUM_QUEUES,
			g_cfg.num_queues);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_THREADS_PER_QUEUE,
			g_cfg.threads_per_queue);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_TEST_DURATION_SEC,
			g_cfg.run_us / 1000000);
	fprintf(stdout, "%s: %" PRIu64 "\n", TAG_REPORT_INTERVAL_SEC,
			g_cfg.report_interval_us / 1000000);
	fprintf(stdout, "%s: %s\n", TAG_MICROSECOND_HISTOGRAMS,
			g_cfg.us_histograms ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_READ_REQS_PER_SEC,
			g_cfg.read_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_WRITE_REQS_PER_SEC,
			g_cfg.write_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_RECORD_BYTES,
			g_cfg.record_bytes);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_RECORD_BYTES_RANGE_MAX,
			g_cfg.record_bytes_rmx);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_LARGE_BLOCK_OP_KBYTES,
			g_cfg.large_block_ops_bytes / 1024);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_REPLICATION_FACTOR,
			g_cfg.replication_factor);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_UPDATE_PCT,
			g_cfg.update_pct);
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_DEFRAG_LWM_PCT,
			g_cfg.defrag_lwm_pct);
	fprintf(stdout, "%s: %s\n", TAG_COMMIT_TO_DEVICE,
			g_cfg.commit_to_device ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n", TAG_COMMIT_MIN_BYTES,
			g_cfg.commit_min_bytes);
	fprintf(stdout, "%s: %s\n", TAG_SCHEDULER_MODE,
			SCHEDULER_MODES[g_cfg.scheduler_mode]);

	fprintf(stdout, "\n");

	fprintf(stdout, "internal read requests per sec: %" PRIu64 "\n",
			g_cfg.internal_read_reqs_per_sec);
	fprintf(stdout, "internal write requests per sec: %" PRIu64 "\n",
			g_cfg.internal_write_reqs_per_sec);
	fprintf(stdout, "bytes per stored record: %" PRIu32 " ... %" PRIu32 "\n",
			g_cfg.record_stored_bytes, g_cfg.record_stored_bytes_rmx);
	fprintf(stdout, "large block reads per sec: %.2lf\n",
			g_cfg.large_block_reads_per_sec);
	fprintf(stdout, "large block writes per sec: %.2lf\n",
			g_cfg.large_block_writes_per_sec);

	fprintf(stdout, "\n");
}

static void
parse_device_names()
{
	const char* val;

	while ((val = strtok(NULL, ",;" WHITE_SPACE)) != NULL) {
		int name_len = strlen(val);

		if (name_len == 0 || name_len >= MAX_DEVICE_NAME_SIZE) {
			continue;
		}

		strcpy(g_cfg.device_names[g_cfg.num_devices], val);

		if (++g_cfg.num_devices >= MAX_NUM_DEVICES) {
			break;
		}
	}
}

static void
parse_scheduler_mode()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	if (! val) {
		return;
	}

	for (uint32_t m = 0; m < NUM_SCHEDULER_MODES; m++) {
		if (strcmp(val, SCHEDULER_MODES[m]) == 0) {
			g_cfg.scheduler_mode = m;
		}
	}
}

static uint32_t
parse_uint32()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	return val ? strtoul(val, NULL, 10) : 0;
}

static bool
parse_yes_no()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	return val && *val == 'y';
}
