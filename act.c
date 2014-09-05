/*
 *	act.c
 *
 *	Aerospike Certifiction Tool - Simulates and Validates SSDs
 *        for real-time database use
 *	Joey Shurtleff & Andrew Gooding, 2011
 *
 * Copyright (c) 2008-2012 Aerospike, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */


//==========================================================
// Includes
//

#include <dirent.h>
#include <execinfo.h>	// for debugging
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>		// for debugging
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fs.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "atomic.h"
#include "clock.h"
#include "histogram.h"
#include "queue.h"


//==========================================================
// Constants
//

const char VERSION[] = "2.0";

const char TAG_DEVICE_NAMES[]				= "device-names";
const char TAG_QUEUE_PER_DEVICE[]			= "queue-per-device";
const char TAG_NUM_QUEUES[]					= "num-queues";
const char TAG_THREADS_PER_QUEUE[]			= "threads-per-queue";
const char TAG_RUN_SEC[]					= "test-duration-sec";
const char TAG_REPORT_INTERVAL_SEC[]		= "report-interval-sec";
const char TAG_MICROSECOND_HISTOGRAMS[]		= "microsecond-histograms";
const char TAG_READ_REQS_PER_SEC[]			= "read-reqs-per-sec";
const char TAG_LARGE_BLOCK_OPS_PER_SEC[]	= "large-block-ops-per-sec";
const char TAG_READ_REQ_NUM_512_BLOCKS[]	= "read-req-num-512-blocks";
const char TAG_LARGE_BLOCK_OP_KBYTES[]		= "large-block-op-kbytes";
const char TAG_USE_VALLOC[]					= "use-valloc";
const char TAG_NUM_WRITE_BUFFERS[]			= "num-write-buffers";
const char TAG_SCHEDULER_MODE[]				= "scheduler-mode";

#define MAX_NUM_DEVICES 32
#define MAX_DEVICE_NAME_SIZE 64
#define WHITE_SPACE " \t\n\r"

const uint32_t MIN_BLOCK_BYTES = 512;
const uint32_t RAND_SEED_SIZE = 64;
const uint64_t MAX_READ_REQS_QUEUED = 100000;

const char* const SCHEDULER_MODES[] = {
	"noop",
	"cfq"
};

const uint32_t NUM_SCHEDULER_MODES = sizeof(SCHEDULER_MODES) / sizeof(char*);

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 00040000
#endif


//==========================================================
// Typedefs
//

typedef struct _device {
	const char* name;
	uint64_t num_512_blocks;
	uint64_t num_large_blocks;
	cf_queue* p_fd_queue;
	pthread_t large_block_read_thread;
	pthread_t large_block_write_thread;
	uint8_t* p_large_block_read_buffer;
	histogram* p_raw_read_histogram;
	char histogram_tag[MAX_DEVICE_NAME_SIZE];
} device;

typedef struct _readreq {
	device* p_device;
	uint64_t offset;
	uint32_t size;
	uint64_t start_time;
} readreq;

typedef struct _readq {
	cf_queue* p_req_queue;
	pthread_t* threads;
} readq;

typedef struct _salter {
	uint8_t* p_buffer;
	pthread_mutex_t lock;
	uint32_t stamp;
} salter;


//==========================================================
// Globals
//

static char g_device_names[MAX_NUM_DEVICES][MAX_DEVICE_NAME_SIZE];
static uint32_t g_num_devices = 0;
static bool g_queue_per_device = false;
static uint32_t g_num_queues = 0;
static uint32_t g_threads_per_queue = 0;
static uint64_t g_run_ms = 0;
static uint32_t g_report_interval_ms = 0;
static bool g_us_histograms = false;
static uint64_t g_read_reqs_per_sec = 0;
static uint64_t g_large_block_ops_per_sec = 0;
static uint32_t g_read_req_num_512_blocks = 0;
static uint32_t g_large_block_ops_bytes = 0;
static bool g_use_valloc = false;
static uint32_t g_num_write_buffers = 0;
static uint32_t g_scheduler_mode = 0;

static salter* g_salters;

static device* g_devices;
static readq* g_readqs;

static uint32_t g_running;
static uint64_t g_run_start_ms;

static cf_atomic_int g_read_reqs_queued = 0;

static histogram* g_p_large_block_read_histogram;
static histogram* g_p_large_block_write_histogram;
static histogram* g_p_raw_read_histogram;
static histogram* g_p_read_histogram;


//==========================================================
// Forward Declarations
//

static void*	run_add_readreqs(void* pv_unused);
static void*	run_large_block_reads(void* pv_device);
static void*	run_large_block_writes(void* pv_device);
static void*	run_reads(void* pv_req_queue);

static inline uint8_t* align_4096(uint8_t* stack_buffer);
static inline uint8_t* cf_valloc(size_t size);
static bool		check_config();
static void		config_parse_device_names();
static void		config_parse_scheduler_mode();
static uint32_t	config_parse_uint32();
static bool		config_parse_yes_no();
static bool		configure(int argc, char* argv[]);
static bool		create_large_block_read_buffer(device* p_device);
static bool		create_salters();
static void		destroy_salters();
static void		discover_num_blocks(device* p_device);
static void		fd_close_all(device* p_device);
static int		fd_get(device* p_device);
static void		fd_put(device* p_device, int fd);
static inline uint32_t rand_31();
static uint64_t	rand_64();
static bool		rand_fill(uint8_t* p_buffer, uint32_t size);
static bool		rand_seed(uint8_t* p_buffer);
static uint64_t	random_read_offset(device* p_device);
static uint64_t	random_large_block_offset(device* p_device);
static void		read_and_report(readreq* p_readreq, uint8_t* p_buffer);
static void		read_and_report_large_block(device* p_device);
static uint64_t	read_from_device(device* p_device, uint64_t offset,
					uint32_t size, uint8_t* p_buffer);
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns);
static void		set_schedulers();
static void		write_and_report_large_block(device* p_device);
static uint64_t	write_to_device(device* p_device, uint64_t offset,
					uint32_t size, uint8_t* p_buffer);

static void		as_sig_handle_segv(int sig_num);
static void		as_sig_handle_term(int sig_num);


//==========================================================
// Main
//

int main(int argc, char* argv[]) {
	signal(SIGSEGV, as_sig_handle_segv);
	signal(SIGTERM , as_sig_handle_term);

	fprintf(stdout, "\nAerospike act version %s - device IO test\n", VERSION);
	fprintf(stdout, "Copyright 2011 by Aerospike. All rights reserved.\n\n");

	if (! configure(argc, argv)) {
		exit(-1);
	}

	set_schedulers();
	srand(time(NULL));

	salter salters[g_num_write_buffers ? g_num_write_buffers : 1];

	g_salters = salters;

	if (! create_salters()) {
		exit(-1);
	}

	device devices[g_num_devices];
	readq readqs[g_num_queues];

	g_devices = devices;
	g_readqs = readqs;

	histogram_scale scale =
			g_us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	g_p_large_block_read_histogram = histogram_create(scale);
	g_p_large_block_write_histogram = histogram_create(scale);
	g_p_raw_read_histogram = histogram_create(scale);
	g_p_read_histogram = histogram_create(scale);

	g_run_start_ms = cf_getms();

	uint64_t run_stop_ms = g_run_start_ms + g_run_ms;

	g_running = 1;

	for (int n = 0; n < g_num_devices; n++) {
		device* p_device = &g_devices[n];

		p_device->name = g_device_names[n];
		p_device->p_fd_queue = cf_queue_create(sizeof(int), true);
		discover_num_blocks(p_device);
		create_large_block_read_buffer(p_device);
		p_device->p_raw_read_histogram = histogram_create(scale);
		sprintf(p_device->histogram_tag, "%-18s", p_device->name);

		if (pthread_create(&p_device->large_block_read_thread, NULL,
				run_large_block_reads, (void*)p_device)) {
			fprintf(stdout, "ERROR: create large block read thread %d\n", n);
			exit(-1);
		}

		if (pthread_create(&p_device->large_block_write_thread, NULL,
				run_large_block_writes, (void*)p_device)) {
			fprintf(stdout, "ERROR: create write thread %d\n", n);
			exit(-1);
		}
	}

	for (int i = 0; i < g_num_queues; i++) {
		readq* p_readq = &g_readqs[i];

		p_readq->p_req_queue = cf_queue_create(sizeof(readreq*), true);
		p_readq->threads = malloc(sizeof(pthread_t) * g_threads_per_queue);

		for (int j = 0; j < g_threads_per_queue; j++) {
			if (pthread_create(&p_readq->threads[j], NULL, run_reads,
					(void*)p_readq->p_req_queue)) {
				fprintf(stdout, "ERROR: create read thread %d:%d\n", i, j);
				exit(-1);
			}
		}
	}

	pthread_t thr_add_readreqs;

	if (pthread_create(&thr_add_readreqs, NULL, run_add_readreqs, NULL)) {
		fprintf(stdout, "ERROR: create thread thr_add_readreqs\n");
		exit(-1);
	}

	fprintf(stdout, "\n");

	uint64_t now_ms;
	uint64_t count = 0;

	while ((now_ms = cf_getms()) < run_stop_ms && g_running) {	
		count++;

		int sleep_ms = (int)
			((count * g_report_interval_ms) - (now_ms - g_run_start_ms));

		if (sleep_ms > 0) {
			usleep((uint32_t)sleep_ms * 1000);
		}

		fprintf(stdout, "After %" PRIu64 " sec:\n",
			(count * g_report_interval_ms) / 1000);

		fprintf(stdout, "read-reqs queued: %" PRIu64 "\n",
			cf_atomic_int_get(g_read_reqs_queued));

		histogram_dump(g_p_large_block_read_histogram,  "LARGE BLOCK READS ");
		histogram_dump(g_p_large_block_write_histogram, "LARGE BLOCK WRITES");
		histogram_dump(g_p_raw_read_histogram,          "RAW READS         ");

		for (int d = 0; d < g_num_devices; d++) {			
			histogram_dump(g_devices[d].p_raw_read_histogram,
				g_devices[d].histogram_tag);	
		}

		histogram_dump(g_p_read_histogram,              "READS             ");
		fprintf(stdout, "\n");
		fflush(stdout);
	}

	g_running = 0;

	void* pv_value;

	pthread_join(thr_add_readreqs, &pv_value);

	for (int i = 0; i < g_num_queues; i++) {
		readq* p_readq = &g_readqs[i];

		for (int j = 0; j < g_threads_per_queue; j++) {
			pthread_join(p_readq->threads[j], &pv_value);
		}

		cf_queue_destroy(p_readq->p_req_queue);
		free(p_readq->threads);
	}

	for (int d = 0; d < g_num_devices; d++) {
		device* p_device = &g_devices[d];

		pthread_join(p_device->large_block_read_thread, &pv_value);
		pthread_join(p_device->large_block_write_thread, &pv_value);

		fd_close_all(p_device);
		cf_queue_destroy(p_device->p_fd_queue);
		free(p_device->p_large_block_read_buffer);
		free(p_device->p_raw_read_histogram);
	}

	free(g_p_large_block_read_histogram);
	free(g_p_large_block_write_histogram);
	free(g_p_raw_read_histogram);
	free(g_p_read_histogram);

	destroy_salters();

	return 0;
}


//==========================================================
// Thread "Run" Functions
//

//------------------------------------------------
// Runs in thr_add_readreqs, adds readreq objects
// to all read queues in an even, random spread.
//
static void* run_add_readreqs(void* pv_unused) {
	uint64_t count = 0;

	while (g_running) {
		if (cf_atomic_int_incr(&g_read_reqs_queued) > MAX_READ_REQS_QUEUED) {
			fprintf(stdout, "ERROR: too many read reqs queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t random_queue_index = rand_31() % g_num_queues;
		uint32_t random_device_index =
			g_queue_per_device ? random_queue_index : rand_31() % g_num_devices;

		device* p_random_device = &g_devices[random_device_index];
		readreq* p_readreq = malloc(sizeof(readreq));

		p_readreq->p_device = p_random_device;
		p_readreq->offset = random_read_offset(p_random_device);
		p_readreq->size = g_read_req_num_512_blocks * MIN_BLOCK_BYTES;
		p_readreq->start_time = cf_getns();

		cf_queue_push(g_readqs[random_queue_index].p_req_queue, &p_readreq);

		count++;

		int sleep_ms = (int)
			(((count * 1000) / g_read_reqs_per_sec) -
				(cf_getms() - g_run_start_ms));

		if (sleep_ms > 0) {
			usleep((uint32_t)sleep_ms * 1000);
		}

//		if (sleep_ms != 0) {
//			fprintf(stdout, "%" PRIu64 ", sleep_ms = %d\n", count, sleep_ms);
//		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block read thread,
// executes large-block reads at a constant rate.
//
static void* run_large_block_reads(void* pv_device) {
	device* p_device = (device*)pv_device;
	uint64_t count = 0;

	while (g_running) {
		read_and_report_large_block(p_device);

		count++;

		int sleep_ms = (int)
			(((count * 1000 * g_num_devices) / g_large_block_ops_per_sec) -
				(cf_getms() - g_run_start_ms));

		if (sleep_ms > 0) {
			usleep((uint32_t)sleep_ms * 1000);
		}

//		if (sleep_ms != 0) {
//			fprintf(stdout, "%" PRIu64 ", sleep_ms = %d\n", count, sleep_ms);
//		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block write thread,
// executes large-block writes at a constant rate.
//
static void* run_large_block_writes(void* pv_device) {
	device* p_device = (device*)pv_device;
	uint64_t count = 0;

	while (g_running) {
		write_and_report_large_block(p_device);

		count++;

		int sleep_ms = (int)
			(((count * 1000 * g_num_devices) / g_large_block_ops_per_sec) -
				(cf_getms() - g_run_start_ms));

		if (sleep_ms > 0) {
			usleep((uint32_t)sleep_ms * 1000);
		}

//		if (sleep_ms != 0) {
//			fprintf(stdout, "%" PRIu64 ", sleep_ms = %d\n", count, sleep_ms);
//		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every thread of every read queue, pops
// readreq objects, does the read and reports the
// read transaction duration.
//
static void* run_reads(void* pv_req_queue) {
	cf_queue* p_req_queue = (cf_queue*)pv_req_queue;
	readreq* p_readreq;

	while (g_running) {
		if (cf_queue_pop(p_req_queue, (void*)&p_readreq, 100) != CF_QUEUE_OK) {
			continue;
		}

		if (g_use_valloc) {
			uint8_t* p_buffer = cf_valloc(p_readreq->size);

			if (p_buffer) {
				read_and_report(p_readreq, p_buffer);
				free(p_buffer);
			}
			else {
				fprintf(stdout, "ERROR: read buffer cf_valloc()\n");
			}
		}
		else {
			uint8_t stack_buffer[p_readreq->size + 4096];
			uint8_t* p_buffer = align_4096(stack_buffer);

			read_and_report(p_readreq, p_buffer);
		}

		free(p_readreq);
		cf_atomic_int_decr(&g_read_reqs_queued);
	}

	return NULL;
}


//==========================================================
// Helpers
//

//------------------------------------------------
// Align stack-allocated memory.
//
static inline uint8_t* align_4096(uint8_t* stack_buffer) {
	return (uint8_t*)(((uint64_t)stack_buffer + 4095) & ~4095ULL);
}

//------------------------------------------------
// Aligned memory allocation.
//
static inline uint8_t* cf_valloc(size_t size) {
	void* pv;

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
}

//------------------------------------------------
// Check (and finish setting) run parameters.
//
static bool check_config() {
	fprintf(stdout, "CIO CONFIGURATION\n");

	fprintf(stdout, "%s:", TAG_DEVICE_NAMES);

	for (int d = 0; d < g_num_devices; d++) {
		fprintf(stdout, " %s", g_device_names[d]);
	}

	fprintf(stdout, "\nnum-devices: %" PRIu32 "\n",
		g_num_devices);
	fprintf(stdout, "%s: %s\n",				TAG_QUEUE_PER_DEVICE,
		g_queue_per_device ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_NUM_QUEUES,
		g_num_queues);
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_THREADS_PER_QUEUE,
		g_threads_per_queue);
	fprintf(stdout, "%s: %" PRIu64 "\n",	TAG_RUN_SEC,
		g_run_ms / 1000);
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_REPORT_INTERVAL_SEC,
		g_report_interval_ms / 1000);
	fprintf(stdout, "%s: %s\n",				TAG_MICROSECOND_HISTOGRAMS,
		g_us_histograms ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu64 "\n",	TAG_READ_REQS_PER_SEC,
		g_read_reqs_per_sec);
	fprintf(stdout, "%s: %" PRIu64 "\n",	TAG_LARGE_BLOCK_OPS_PER_SEC,
		g_large_block_ops_per_sec);
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_READ_REQ_NUM_512_BLOCKS,
		g_read_req_num_512_blocks);
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_LARGE_BLOCK_OP_KBYTES,
		g_large_block_ops_bytes / 1024);
	fprintf(stdout, "%s: %s\n",				TAG_USE_VALLOC,
		g_use_valloc ? "yes" : "no");
	fprintf(stdout, "%s: %" PRIu32 "\n",	TAG_NUM_WRITE_BUFFERS,
		g_num_write_buffers);
	fprintf(stdout, "%s: %s\n",				TAG_SCHEDULER_MODE,
		SCHEDULER_MODES[g_scheduler_mode]);
	fprintf(stdout, "\n");

	if (! (	g_num_devices &&
			g_num_queues &&
			g_threads_per_queue &&
			g_run_ms &&
			g_report_interval_ms &&
			g_read_reqs_per_sec &&
			g_large_block_ops_per_sec &&
			g_read_req_num_512_blocks &&
			g_large_block_ops_bytes)) {
		fprintf(stdout, "ERROR: invalid configuration\n");
		return false;
	}

	return true;
}

//------------------------------------------------
// Parse device names parameter.
//
static void config_parse_device_names() {
	const char* val;

	while ((val = strtok(NULL, ",;" WHITE_SPACE)) != NULL) {
		int name_length = strlen(val);

		if (name_length == 0 || name_length >= MAX_DEVICE_NAME_SIZE) {
			continue;
		}

		strcpy(g_device_names[g_num_devices], val);
		g_num_devices++;

		if (g_num_devices >= MAX_NUM_DEVICES) {
			break;
		}
	}
}

//------------------------------------------------
// Parse system block scheduler mode.
//
static void config_parse_scheduler_mode() {
	const char* val = strtok(NULL, WHITE_SPACE);

	if (! val) {
		return;
	}

	for (uint32_t m = 0; m < NUM_SCHEDULER_MODES; m++) {
		if (! strcmp(val, SCHEDULER_MODES[m])) {
			g_scheduler_mode = m;
		}
	}
}

//------------------------------------------------
// Parse numeric run parameter.
//
static uint32_t config_parse_uint32() {
	const char* val = strtok(NULL, WHITE_SPACE);

	return val ? strtoul(val, NULL, 10) : 0;
}

//------------------------------------------------
// Parse yes/no run parameter.
//
static bool config_parse_yes_no() {
	const char* val = strtok(NULL, WHITE_SPACE);

	return val && *val == 'y';
}

//------------------------------------------------
// Set run parameters.
//
static bool configure(int argc, char* argv[]) {
	if (argc != 2) {
		fprintf(stdout, "usage: act [config filename]\n");
		return false;
	}

	FILE* config_file = fopen(argv[1], "r");

	if (! config_file) {
		fprintf(stdout, "couldn't open config file: %s\n", argv[1]);
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

		if		(! strcmp(tag, TAG_DEVICE_NAMES)) {
			config_parse_device_names();
		}
		else if (! strcmp(tag, TAG_QUEUE_PER_DEVICE)) {
			g_queue_per_device = config_parse_yes_no();
		}
		else if (! strcmp(tag, TAG_NUM_QUEUES)) {
			g_num_queues = config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_THREADS_PER_QUEUE)) {
			g_threads_per_queue = config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_RUN_SEC)) {
			g_run_ms = (uint64_t)config_parse_uint32() * 1000;
		}
		else if (! strcmp(tag, TAG_REPORT_INTERVAL_SEC)) {
			g_report_interval_ms = config_parse_uint32() * 1000;
		}
		else if (! strcmp(tag, TAG_MICROSECOND_HISTOGRAMS)) {
			g_us_histograms = config_parse_yes_no();
		}
		else if (! strcmp(tag, TAG_READ_REQS_PER_SEC)) {
			g_read_reqs_per_sec = (uint64_t)config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_LARGE_BLOCK_OPS_PER_SEC)) {
			g_large_block_ops_per_sec = (uint64_t)config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_READ_REQ_NUM_512_BLOCKS)) {
			g_read_req_num_512_blocks = config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_LARGE_BLOCK_OP_KBYTES)) {
			g_large_block_ops_bytes = config_parse_uint32() * 1024;
		}
		else if (! strcmp(tag, TAG_USE_VALLOC)) {
			g_use_valloc = config_parse_yes_no();
		}
		else if (! strcmp(tag, TAG_NUM_WRITE_BUFFERS)) {
			g_num_write_buffers = config_parse_uint32();
		}
		else if (! strcmp(tag, TAG_SCHEDULER_MODE)) {
			config_parse_scheduler_mode();
		}
	}

	fclose(config_file);

	if (g_queue_per_device) {
		g_num_queues = g_num_devices;
	}

	return check_config();
}

//------------------------------------------------
// Create device large block read buffer.
//
static bool create_large_block_read_buffer(device* p_device) {
	if (! (p_device->p_large_block_read_buffer =
			cf_valloc(g_large_block_ops_bytes))) {
		fprintf(stdout, "ERROR: large block read buffer cf_valloc()\n");
		return false;
	}

	return true;
}

//------------------------------------------------
// Create large block write buffers.
//
static bool create_salters() {
	if (! g_num_write_buffers) {
		if (! (g_salters[0].p_buffer = cf_valloc(g_large_block_ops_bytes))) {
			fprintf(stdout, "ERROR: large block write buffer cf_valloc()\n");
			return false;
		}

		memset(g_salters[0].p_buffer, 0, g_large_block_ops_bytes);
		g_num_write_buffers = 1;

		return true;
	}

	uint8_t seed_buffer[RAND_SEED_SIZE];

	if (! rand_seed(seed_buffer)) {
		return false;
	}

	for (uint32_t n = 0; n < g_num_write_buffers; n++) {
		if (! (g_salters[n].p_buffer = cf_valloc(g_large_block_ops_bytes))) {
			fprintf(stdout, "ERROR: large block write buffer cf_valloc()\n");
			return false;
		}

		if (! rand_fill(g_salters[n].p_buffer, g_large_block_ops_bytes)) {
			return false;
		}

		if (g_num_write_buffers > 1) {
			pthread_mutex_init(&g_salters[n].lock, NULL);
		}
	}

	return true;
}

//------------------------------------------------
// Destroy large block write buffers.
//
static void destroy_salters() {
	for (uint32_t n = 0; n < g_num_write_buffers; n++) {
		free(g_salters[n].p_buffer);

		if (g_num_write_buffers > 1) {
			pthread_mutex_destroy(&g_salters[n].lock);
		}
	}
}

//------------------------------------------------
// Discover device storage capacity.
//
static void discover_num_blocks(device* p_device) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		p_device->num_512_blocks = 0;
		p_device->num_large_blocks = 0;
		return;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	p_device->num_large_blocks = device_bytes / g_large_block_ops_bytes;
	p_device->num_512_blocks = 
		(p_device->num_large_blocks * g_large_block_ops_bytes) /
			MIN_BLOCK_BYTES;

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64
		" 512-byte blocks, %" PRIu64 " large blocks\n", p_device->name,
			device_bytes, p_device->num_512_blocks, p_device->num_large_blocks);

	fd_put(p_device, fd);
}

//------------------------------------------------
// Close all file descriptors for a device.
//
static void fd_close_all(device* p_device) {
	int fd;

	while (cf_queue_pop(p_device->p_fd_queue, (void*)&fd, CF_QUEUE_NOWAIT) ==
			CF_QUEUE_OK) {
		close(fd);
	}
}

//------------------------------------------------
// Get a safe file descriptor for a device.
//
static int fd_get(device* p_device) {
	int fd = -1;

	if (cf_queue_pop(p_device->p_fd_queue, (void*)&fd, CF_QUEUE_NOWAIT) !=
			CF_QUEUE_OK) {
		fd = open(p_device->name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);

		if (fd == -1) {
			fprintf(stdout, "ERROR: open device %s\n", p_device->name);
		}
	}

	return (fd);
}

//------------------------------------------------
// Recycle a safe file descriptor for a device.
//
static void fd_put(device* p_device, int fd) {
	cf_queue_push(p_device->p_fd_queue, (void*)&fd);
}

//------------------------------------------------
// Get a random 31-bit uint32_t.
//
static inline uint32_t rand_31() {
	return (uint32_t)rand();
}

//------------------------------------------------
// Get a random uint64_t.
//
static uint64_t rand_64() {
	return ((uint64_t)rand() << 16) | ((uint64_t)rand() & 0xffffULL);
}

//------------------------------------------------
// Fill a buffer (> 64 bytes) with random bits.
//
static bool rand_fill(uint8_t* p_buffer, uint32_t size) {
	if (RAND_bytes(p_buffer, size) != 1) {
		fprintf(stdout, "ERROR: RAND_bytes() failed\n");
		return false;
	}

	return true;
}

//------------------------------------------------
// Seed a buffer (> 64 bytes) for random fill.
//
static bool rand_seed(uint8_t* p_buffer) {
	int fd = open("/dev/urandom", O_RDONLY);

	if (fd == -1) {
		fprintf(stdout, "ERROR: can't open /dev/urandom\n");
		return false;
	}

	ssize_t read_result = read(fd, p_buffer, RAND_SEED_SIZE);

	if (read_result != (ssize_t)RAND_SEED_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: can't seed random number generator\n");
		return false;
	}

	close(fd);
	RAND_seed(p_buffer, read_result);

	return true;
}

//------------------------------------------------
// Get a random read offset for a device.
//
static uint64_t random_read_offset(device* p_device) {
	if (! p_device->num_512_blocks) {
		return 0;
	}

	uint64_t num_read_offsets =
		p_device->num_512_blocks - (uint64_t)g_read_req_num_512_blocks + 1;

	return (rand_64() % num_read_offsets) * MIN_BLOCK_BYTES;
}

//------------------------------------------------
// Get a random large block offset for a device.
//
static uint64_t random_large_block_offset(device* p_device) {
	if (! p_device->num_large_blocks) {
		return 0;
	}

	return (rand_64() % p_device->num_large_blocks) * g_large_block_ops_bytes;
}

//------------------------------------------------
// Do one transaction read operation and report.
//
static void read_and_report(readreq* p_readreq, uint8_t* p_buffer) {
	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_readreq->p_device,
		p_readreq->offset, p_readreq->size, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_read_histogram,
			safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_p_read_histogram,
			safe_delta_ns(p_readreq->start_time, stop_time));
		histogram_insert_data_point(
			p_readreq->p_device->p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one large block read operation and report.
//
static void read_and_report_large_block(device* p_device) {
	uint64_t offset = random_large_block_offset(p_device);
	uint64_t start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_device, offset,
		g_large_block_ops_bytes, p_device->p_large_block_read_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_large_block_read_histogram,
			safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device read operation.
//
static uint64_t read_from_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		fprintf(stdout, "ERROR: seek & read\n");
		return -1;
	}

	uint64_t stop_ns = cf_getns();

	fd_put(p_device, fd);

	return stop_ns;
}

//------------------------------------------------
// Check time differences.
//
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns) {
	return start_ns > stop_ns ? 0 : stop_ns - start_ns;
}

//------------------------------------------------
// Set devices' system block schedulers.
//
static void set_schedulers() {
	const char* mode = SCHEDULER_MODES[g_scheduler_mode];
	size_t mode_length = strlen(mode);

	for (uint32_t d = 0; d < g_num_devices; d++) {
		const char* device_name = g_device_names[d];
		const char* p_slash = strrchr(device_name, '/');
		const char* device_tag = p_slash ? p_slash + 1 : device_name;

		char scheduler_file_name[128];

		strcpy(scheduler_file_name, "/sys/block/");
		strcat(scheduler_file_name, device_tag);
		strcat(scheduler_file_name, "/queue/scheduler");

		FILE* scheduler_file = fopen(scheduler_file_name, "w");

		if (! scheduler_file) {
			fprintf(stdout, "ERROR: couldn't open %s\n", scheduler_file_name);
			continue;
		}

		if (fwrite(mode, mode_length, 1, scheduler_file) != 1) {
			fprintf(stdout, "ERROR: writing %s to %s\n", mode,
				scheduler_file_name);
		}

		fclose(scheduler_file);
	}
}

//------------------------------------------------
// Do one large block write operation and report.
//
static void write_and_report_large_block(device* p_device) {
	salter* p_salter;

	if (g_num_write_buffers > 1) {
		p_salter = &g_salters[rand_31() % g_num_write_buffers];

		pthread_mutex_lock(&p_salter->lock);
		*(uint32_t*)p_salter->p_buffer = p_salter->stamp++;
	}
	else {
		p_salter = &g_salters[0];
	}

	uint64_t offset = random_large_block_offset(p_device);
	uint64_t start_time = cf_getns();
	uint64_t stop_time = write_to_device(p_device, offset,
		g_large_block_ops_bytes, p_salter->p_buffer);

	if (g_num_write_buffers > 1) {
		pthread_mutex_unlock(&p_salter->lock);
	}

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_large_block_write_histogram,
			safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t write_to_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			write(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		fprintf(stdout, "ERROR: seek & write\n");
		return -1;
	}

	uint64_t stop_ns = cf_getns();

	fd_put(p_device, fd);

	return stop_ns;
}


//==========================================================
// Debugging Helpers
//

static void as_sig_handle_segv(int sig_num) {
	fprintf(stdout, "Signal SEGV received: stack trace\n");

	void* bt[50];
	uint sz = backtrace(bt, 50);
	
	char** strings = backtrace_symbols(bt, sz);

	for (int i = 0; i < sz; ++i) {
		fprintf(stdout, "stacktrace: frame %d: %s\n", i, strings[i]);
	}

	free(strings);
	
	fflush(stdout);
	_exit(-1);
}

static void as_sig_handle_term(int sig_num) {
	fprintf(stdout, "Signal TERM received, aborting\n");

  	void* bt[50];
	uint sz = backtrace(bt, 50);

	char** strings = backtrace_symbols(bt, sz);

	for (int i = 0; i < sz; ++i) {
		fprintf(stdout, "stacktrace: frame %d: %s\n", i, strings[i]);
	}

	free(strings);

	fflush(stdout);
	_exit(0);
}
