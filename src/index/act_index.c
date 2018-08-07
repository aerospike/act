/*
 * act_index.c
 *
 * Aerospike Index Certifiction Tool - Simulates and validates primary index
 * SSDs for real-time database use.
 *
 * Kevin Porter & Andrew Gooding, 2018.
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

#include <dirent.h>
#include <execinfo.h>	// for debugging
#include <errno.h>
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
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "common/atomic.h"
#include "common/cfg.h"
#include "common/clock.h"
#include "common/histogram.h"
#include "common/queue.h"
#include "common/random.h"
#include "common/version.h"

#include "cfg_index.h"


//==========================================================
// Typedefs & constants.
//

typedef struct _device {
	const char* name;
	uint32_t n;
	uint64_t num_io_offsets;
	cf_queue* p_fd_queue;
	histogram* p_raw_read_histogram;
	histogram* p_raw_write_histogram;
	char histogram_tag[MAX_DEVICE_NAME_SIZE];
} device;

typedef struct _trans_req {
	device* p_device;
	uint64_t offset;
	uint64_t start_time;
} trans_req;

typedef struct _transq {
	cf_queue* p_req_queue;
	pthread_t* threads;
} transq;

#define IO_SIZE 4096

#define BUNDLE_SIZE 100

static const uint32_t MAX_REQS_QUEUED = 100000;
static const int64_t MAX_SLEEP_LAG_USEC = 1000000 * 10;

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif


//==========================================================
// Forward declarations.
//

static void* run_cache_simulation(void* pv_unused);
static void* run_generate_rw_reqs(void* pv_unused);
static void* run_transactions(void* pv_req_queue);

static inline uint8_t* align_4096(const uint8_t* stack_buffer);
static bool discover_device(device* p_device);
static void fd_close_all(device* p_device);
static int fd_get(device* p_device);
static void fd_put(device* p_device, int fd);
static inline uint32_t rand_31();
static inline uint64_t rand_48();
static inline uint64_t random_io_offset(const device* p_device);
static void read_and_report(trans_req* p_read_req, uint8_t* p_buffer);
static void read_cache_and_report(uint8_t* p_buffer);
static uint64_t read_from_device(device* p_device, uint64_t offset,
		uint8_t* p_buffer);
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns);
static void set_schedulers();
static void write_cache_and_report(uint8_t* p_buffer);
static uint64_t write_to_device(device* p_device, uint64_t offset,
		uint8_t* p_buffer);

static void as_sig_handle_segv(int sig_num);
static void as_sig_handle_term(int sig_num);


//==========================================================
// Globals.
//

static device* g_devices;
static transq* g_transqs;

static volatile bool g_running;
static uint64_t g_run_start_us;

static cf_atomic32 g_reqs_queued = 0;

static histogram* g_p_raw_read_histogram;
static histogram* g_p_raw_write_histogram;
static histogram* g_p_trans_read_histogram;


//==========================================================
// Main.
//

int
main(int argc, char* argv[])
{
	signal(SIGSEGV, as_sig_handle_segv);
	signal(SIGTERM , as_sig_handle_term);

	fprintf(stdout, "\nAerospike act version %s\n", VERSION);
	fprintf(stdout, "Index device IO test\n");
	fprintf(stdout, "Copyright 2018 by Aerospike. All rights reserved.\n\n");

	if (! index_configure(argc, argv)) {
		exit(-1);
	}

	set_schedulers();

	srand(time(NULL));

	if (! rand_seed()) {
		exit(-1);
	}

	device devices[g_icfg.num_devices];
	transq transqs[g_icfg.num_queues];

	g_devices = devices;
	g_transqs = transqs;

	histogram_scale scale =
			g_icfg.us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	if (! (g_p_raw_read_histogram = histogram_create(scale)) ||
		! (g_p_raw_write_histogram = histogram_create(scale)) ||
		! (g_p_trans_read_histogram = histogram_create(scale))) {
		exit(-1);
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* p_device = &g_devices[d];

		p_device->name = g_icfg.device_names[d];
		p_device->n = d;

		if (! (p_device->p_fd_queue = cf_queue_create(sizeof(int), true)) ||
			! discover_device(p_device) ||
			! (p_device->p_raw_read_histogram = histogram_create(scale)) ||
			! (p_device->p_raw_write_histogram = histogram_create(scale))) {
			exit(-1);
		}

		sprintf(p_device->histogram_tag, "%-18s", p_device->name);
	}

	g_run_start_us = cf_getus();

	uint64_t run_stop_us = g_run_start_us + g_icfg.run_us;

	g_running = true;

	pthread_t cache_threads[g_icfg.num_cache_threads];
	bool has_write_load = g_icfg.cache_thread_reads_and_writes_per_sec != 0;

	if (has_write_load) {
		for (uint32_t n = 0; n < g_icfg.num_cache_threads; n++) {
			if (pthread_create(&cache_threads[n], NULL, run_cache_simulation,
					NULL) != 0) {
				fprintf(stdout, "ERROR: create cache thread\n");
				exit(-1);
			}
		}
	}

	for (uint32_t i = 0; i < g_icfg.num_queues; i++) {
		transq* p_transq = &g_transqs[i];

		if (! (p_transq->p_req_queue =
				cf_queue_create(sizeof(trans_req), true))) {
			exit(-1);
		}

		if (! (p_transq->threads =
				malloc(sizeof(pthread_t) * g_icfg.threads_per_queue))) {
			fprintf(stdout, "ERROR: malloc transaction threads array\n");
			exit(-1);
		}

		for (uint32_t j = 0; j < g_icfg.threads_per_queue; j++) {
			if (pthread_create(&p_transq->threads[j], NULL, run_transactions,
					(void*)p_transq->p_req_queue) != 0) {
				fprintf(stdout, "ERROR: create transaction thread\n");
				exit(-1);
			}
		}
	}

	pthread_t rw_req_generator;

	if (pthread_create(&rw_req_generator, NULL, run_generate_rw_reqs,
			NULL) != 0) {
		fprintf(stdout, "ERROR: create read request generator thread\n");
		exit(-1);
	}

	fprintf(stdout, "\n");

	uint64_t now_us = 0;
	uint64_t count = 0;

	while (g_running && (now_us = cf_getus()) < run_stop_us) {
		count++;

		int64_t sleep_us = (int64_t)
				((count * g_icfg.report_interval_us) -
						(now_us - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}

		fprintf(stdout, "After %" PRIu64 " sec:\n",
				(count * g_icfg.report_interval_us) / 1000000);

		fprintf(stdout, "requests queued: %" PRIu32 "\n",
				cf_atomic32_get(g_reqs_queued));

		histogram_dump(g_p_raw_read_histogram,      "RAW READS  ");

		for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
			histogram_dump(g_devices[d].p_raw_read_histogram,
					g_devices[d].histogram_tag);
		}

		histogram_dump(g_p_trans_read_histogram,    "TRANS READS");

		if (has_write_load) {
			histogram_dump(g_p_raw_write_histogram, "RAW WRITES ");

			for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
				histogram_dump(g_devices[d].p_raw_write_histogram,
						g_devices[d].histogram_tag);
			}
		}

		fprintf(stdout, "\n");
		fflush(stdout);
	}

	g_running = false;

	void* pv_value;

	pthread_join(rw_req_generator, &pv_value);

	for (uint32_t i = 0; i < g_icfg.num_queues; i++) {
		transq* p_transq = &g_transqs[i];

		for (uint32_t j = 0; j < g_icfg.threads_per_queue; j++) {
			pthread_join(p_transq->threads[j], &pv_value);
		}

		cf_queue_destroy(p_transq->p_req_queue);
		free(p_transq->threads);
	}

	if (has_write_load) {
		for (uint32_t n = 0; n < g_icfg.num_cache_threads; n++) {
			pthread_join(cache_threads[n], &pv_value);
		}
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* p_device = &g_devices[d];

		fd_close_all(p_device);
		cf_queue_destroy(p_device->p_fd_queue);
		free(p_device->p_raw_read_histogram);
		free(p_device->p_raw_write_histogram);
	}

	free(g_p_raw_read_histogram);
	free(g_p_raw_write_histogram);
	free(g_p_trans_read_histogram);

	return 0;
}


//==========================================================
// Helpers - thread "run" functions.
//

//------------------------------------------------
//
//
static void*
run_cache_simulation(void* pv_unused)
{
	uint8_t stack_buffer[IO_SIZE + 4096];
	uint8_t* p_buffer = align_4096(stack_buffer);

	uint64_t count = 0;

	while (g_running) {
		for (uint32_t i = 0; i < BUNDLE_SIZE; i++) {
			read_cache_and_report(p_buffer);
			write_cache_and_report(p_buffer);
		}

		count += BUNDLE_SIZE;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_icfg.num_devices) /
						g_icfg.cache_thread_reads_and_writes_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (cf_getus() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -MAX_SLEEP_LAG_USEC) {
			fprintf(stdout, "ERROR: cache thread device IO can't keep up\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in single thread, adds r/w trans_req
// objects to transaction queues in round-robin
// fashion.
//
static void*
run_generate_rw_reqs(void* pv_unused)
{
	uint64_t count = 0;

	while (g_running) {
		if (cf_atomic32_incr(&g_reqs_queued) > MAX_REQS_QUEUED) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t queue_index = count % g_icfg.num_queues;
		uint32_t random_device_index = rand_31() % g_icfg.num_devices;
		device* p_random_device = &g_devices[random_device_index];

		trans_req rw_req = {
				.p_device = p_random_device,
				.offset = random_io_offset(p_random_device),
				.start_time = cf_getns()
		};

		cf_queue_push(g_transqs[queue_index].p_req_queue, &rw_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / g_icfg.trans_thread_reads_per_sec) -
						(cf_getus() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every transaction thread, pops
// trans_req objects, does the transaction and
// reports the duration.
//
static void*
run_transactions(void* pv_req_queue)
{
	cf_queue* p_req_queue = (cf_queue*)pv_req_queue;
	trans_req req;

	while (g_running) {
		if (cf_queue_pop(p_req_queue, (void*)&req, 100) != CF_QUEUE_OK) {
			continue;
		}

		uint8_t stack_buffer[IO_SIZE + 4096];
		uint8_t* p_buffer = align_4096(stack_buffer);

		read_and_report(&req, p_buffer);

		cf_atomic32_decr(&g_reqs_queued);
	}

	return NULL;
}


//==========================================================
// Local helpers - generic.
//

//------------------------------------------------
// Align stack-allocated memory.
//
static inline uint8_t*
align_4096(const uint8_t* stack_buffer)
{
	return (uint8_t*)(((uint64_t)stack_buffer + 4095) & ~4095ULL);
}

//------------------------------------------------
// Discover device storage capacity, etc.
//
static bool
discover_device(device* p_device)
{
	int fd = fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	fd_put(p_device, fd);

	if (device_bytes == 0) {
		fprintf(stdout, "ERROR: %s ioctl to discover size\n", p_device->name);
		return false;
	}

	fprintf(stdout, "%s size = %" PRIu64 " bytes\n", p_device->name,
			device_bytes);

	p_device->num_io_offsets = device_bytes / IO_SIZE;

	return true;
}

//------------------------------------------------
// Close all file descriptors for a device.
//
static void
fd_close_all(device* p_device)
{
	int fd;

	while (cf_queue_pop(p_device->p_fd_queue, (void*)&fd, CF_QUEUE_NOWAIT) ==
			CF_QUEUE_OK) {
		close(fd);
	}
}

//------------------------------------------------
// Get a safe file descriptor for a device.
//
static int
fd_get(device* p_device)
{
	int fd = -1;

	if (cf_queue_pop(p_device->p_fd_queue, (void*)&fd, CF_QUEUE_NOWAIT) !=
			CF_QUEUE_OK) {
		fd = open(p_device->name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);

		if (fd == -1) {
			fprintf(stdout, "ERROR: open device %s errno %d '%s'\n",
					p_device->name, errno, strerror(errno));
		}
	}

	return fd;
}

//------------------------------------------------
// Recycle a safe file descriptor for a device.
//
static void
fd_put(device* p_device, int fd)
{
	cf_queue_push(p_device->p_fd_queue, (void*)&fd);
}

//------------------------------------------------
// Get a random 31-bit uint32_t.
//
static inline uint32_t
rand_31()
{
	return (uint32_t)rand();
}

//------------------------------------------------
// Get a random 48-bit uint64_t.
//
static inline uint64_t
rand_48()
{
	return ((uint64_t)rand() << 16) | ((uint64_t)rand() & 0xffffULL);
}

//------------------------------------------------
// Get a random read offset for a device.
//
static inline uint64_t
random_io_offset(const device* p_device)
{
	return (rand_48() % p_device->num_io_offsets) * IO_SIZE;
}

//------------------------------------------------
// Do one transaction read operation and report.
//
static void
read_and_report(trans_req* p_read_req, uint8_t* p_buffer)
{
	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_read_req->p_device,
			p_read_req->offset, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_p_trans_read_histogram,
				safe_delta_ns(p_read_req->start_time, stop_time));
		histogram_insert_data_point(p_read_req->p_device->p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one cache thread read operation and report.
//
static void
read_cache_and_report(uint8_t* p_buffer)
{
	uint32_t random_device_index = rand_31() % g_icfg.num_devices;
	device* p_device = &g_devices[random_device_index];
	uint64_t offset = random_io_offset(p_device);

	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_device, offset, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(p_device->p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device read operation.
//
static uint64_t
read_from_device(device* p_device, uint64_t offset, uint8_t* p_buffer)
{
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, IO_SIZE) != IO_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: seek & read errno %d '%s'\n", errno,
				strerror(errno));
		return -1;
	}

	uint64_t stop_ns = cf_getns();

	fd_put(p_device, fd);

	return stop_ns;
}

//------------------------------------------------
// Check time differences.
//
static inline uint64_t
safe_delta_ns(uint64_t start_ns, uint64_t stop_ns)
{
	return start_ns > stop_ns ? 0 : stop_ns - start_ns;
}

//------------------------------------------------
// Set devices' system block schedulers.
//
static void
set_schedulers()
{
	const char* mode = SCHEDULER_MODES[g_icfg.scheduler_mode];
	size_t mode_length = strlen(mode);

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		const char* device_name = g_icfg.device_names[d];
		const char* p_slash = strrchr(device_name, '/');
		const char* device_tag = p_slash ? p_slash + 1 : device_name;

		char scheduler_file_name[128];

		strcpy(scheduler_file_name, "/sys/block/");
		strcat(scheduler_file_name, device_tag);
		strcat(scheduler_file_name, "/queue/scheduler");

		FILE* scheduler_file = fopen(scheduler_file_name, "w");

		if (! scheduler_file) {
			fprintf(stdout, "ERROR: couldn't open %s errno %d '%s'\n",
					scheduler_file_name, errno, strerror(errno));
			continue;
		}

		if (fwrite(mode, mode_length, 1, scheduler_file) != 1) {
			fprintf(stdout, "ERROR: writing %s to %s errno %d '%s'\n", mode,
					scheduler_file_name, errno, strerror(errno));
		}

		fclose(scheduler_file);
	}
}

//------------------------------------------------
// Do one cache thread write operation and report.
//
static void
write_cache_and_report(uint8_t* p_buffer)
{
	// Salt the buffer each time.
	rand_fill(p_buffer, IO_SIZE);

	uint32_t random_device_index = rand_31() % g_icfg.num_devices;
	device* p_device = &g_devices[random_device_index];
	uint64_t offset = random_io_offset(p_device);

	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = write_to_device(p_device, offset, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_write_histogram,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(p_device->p_raw_write_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t
write_to_device(device* p_device, uint64_t offset, uint8_t* p_buffer)
{
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			write(fd, p_buffer, IO_SIZE) != IO_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: seek & write errno %d '%s'\n", errno,
				strerror(errno));
		return -1;
	}

	uint64_t stop_ns = cf_getns();

	fd_put(p_device, fd);

	return stop_ns;
}


//==========================================================
// Local helpers - debugging only.
//

static void
as_sig_handle_segv(int sig_num)
{
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

static void
as_sig_handle_term(int sig_num)
{
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
