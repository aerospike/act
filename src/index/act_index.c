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
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
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
#include "common/hardware.h"
#include "common/histogram.h"
#include "common/queue.h"
#include "common/random.h"
#include "common/trace.h"
#include "common/version.h"

#include "cfg_index.h"


//==========================================================
// Typedefs & constants.
//

typedef struct device_s {
	const char* name;
	uint64_t n_io_offsets;
	queue* fd_q;
	histogram* raw_read_hist;
	histogram* raw_write_hist;
	char hist_tag[MAX_DEVICE_NAME_SIZE];
} device;

typedef struct trans_req_s {
	device* dev;
	uint64_t offset;
	uint64_t start_time;
} trans_req;

#define IO_SIZE 4096
#define BUNDLE_SIZE 100

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif


//==========================================================
// Forward declarations.
//

static void* run_cache_simulation(void* pv_unused);
static void* run_generate_rw_reqs(void* pv_unused);
static void* run_transactions(void* pv_req_q);

static bool discover_device(device* dev);
static void fd_close_all(device* dev);
static int fd_get(device* dev);
static void fd_put(device* dev, int fd);
static void read_and_report(trans_req* read_req, uint8_t* buf);
static void read_cache_and_report(uint8_t* buf);
static uint64_t read_from_device(device* dev, uint64_t offset, uint8_t* buf);
static void write_cache_and_report(uint8_t* buf);
static uint64_t write_to_device(device* dev, uint64_t offset,
		const uint8_t* buf);


//==========================================================
// Globals.
//

static device* g_devices;
static queue** g_trans_qs;

static volatile bool g_running;
static uint64_t g_run_start_us;

static atomic32 g_reqs_queued = 0;

static histogram* g_raw_read_hist;
static histogram* g_raw_write_hist;
static histogram* g_trans_read_hist;


//==========================================================
// Inlines & macros.
//

static inline uint8_t*
align_4096(const uint8_t* stack_buffer)
{
	return (uint8_t*)(((uint64_t)stack_buffer + 4095) & ~4095ULL);
}

static inline uint64_t
random_io_offset(const device* dev)
{
	return (rand_64() % dev->n_io_offsets) * IO_SIZE;
}

static inline uint64_t
safe_delta_ns(uint64_t start_ns, uint64_t stop_ns)
{
	return start_ns > stop_ns ? 0 : stop_ns - start_ns;
}


//==========================================================
// Main.
//

int
main(int argc, char* argv[])
{
	signal_setup();

	fprintf(stdout, "\nAerospike ACT version %s\n", VERSION);
	fprintf(stdout, "Index device IO test\n");
	fprintf(stdout, "Copyright 2018 by Aerospike. All rights reserved.\n\n");

	if (! index_configure(argc, argv)) {
		exit(-1);
	}

	device devices[g_icfg.num_devices];
	queue* trans_qs[g_icfg.num_queues];

	g_devices = devices;
	g_trans_qs = trans_qs;

	histogram_scale scale =
			g_icfg.us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	if (! (g_raw_read_hist = histogram_create(scale)) ||
		! (g_raw_write_hist = histogram_create(scale)) ||
		! (g_trans_read_hist = histogram_create(scale))) {
		exit(-1);
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* dev = &g_devices[d];

		dev->name = (const char*)g_icfg.device_names[d];
		set_scheduler(dev->name, g_icfg.scheduler_mode);

		if (! (dev->fd_q = queue_create(sizeof(int), true)) ||
			! discover_device(dev) ||
			! (dev->raw_read_hist = histogram_create(scale)) ||
			! (dev->raw_write_hist = histogram_create(scale))) {
			exit(-1);
		}

		sprintf(dev->hist_tag, "%-18s", dev->name);
	}

	rand_seed();

	g_run_start_us = get_us();

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

	uint32_t n_trans_tids = g_icfg.num_queues * g_icfg.threads_per_queue;
	pthread_t trans_tids[n_trans_tids];

	for (uint32_t i = 0; i < g_icfg.num_queues; i++) {
		if (! (g_trans_qs[i] = queue_create(sizeof(trans_req), true))) {
			exit(-1);
		}

		for (uint32_t j = 0; j < g_icfg.threads_per_queue; j++) {
			if (pthread_create(&trans_tids[(i * g_icfg.threads_per_queue) + j],
					NULL, run_transactions, (void*)g_trans_qs[i]) != 0) {
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

	while (g_running && (now_us = get_us()) < run_stop_us) {
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
				atomic32_get(g_reqs_queued));

		histogram_dump(g_raw_read_hist,      "RAW READS  ");

		for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
			histogram_dump(g_devices[d].raw_read_hist,
					g_devices[d].hist_tag);
		}

		histogram_dump(g_trans_read_hist,    "TRANS READS");

		if (has_write_load) {
			histogram_dump(g_raw_write_hist, "RAW WRITES ");

			for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
				histogram_dump(g_devices[d].raw_write_hist,
						g_devices[d].hist_tag);
			}
		}

		fprintf(stdout, "\n");
		fflush(stdout);
	}

	g_running = false;

	pthread_join(rw_req_generator, NULL);

	for (uint32_t j = 0; j < n_trans_tids; j++) {
		pthread_join(trans_tids[j], NULL);
	}

	for (uint32_t i = 0; i < g_icfg.num_queues; i++) {
		queue_destroy(g_trans_qs[i]);
	}

	if (has_write_load) {
		for (uint32_t n = 0; n < g_icfg.num_cache_threads; n++) {
			pthread_join(cache_threads[n], NULL);
		}
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* dev = &g_devices[d];

		fd_close_all(dev);
		queue_destroy(dev->fd_q);
		free(dev->raw_read_hist);
		free(dev->raw_write_hist);
	}

	free(g_raw_read_hist);
	free(g_raw_write_hist);
	free(g_trans_read_hist);

	return 0;
}


//==========================================================
// Local helpers - thread "run" functions.
//

//------------------------------------------------
// Runs in every (mmap) cache simulation thread,
// does all writes, and reads that don't occur in
// transaction threads, i.e. reads due to defrag.
//
static void*
run_cache_simulation(void* pv_unused)
{
	rand_seed_thread();

	uint8_t stack_buffer[IO_SIZE + 4096];
	uint8_t* buf = align_4096(stack_buffer);

	uint64_t count = 0;

	while (g_running) {
		for (uint32_t i = 0; i < BUNDLE_SIZE; i++) {
			read_cache_and_report(buf);
			write_cache_and_report(buf);
		}

		count += BUNDLE_SIZE;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_icfg.num_devices) /
						g_icfg.cache_thread_reads_and_writes_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -(int64_t)g_icfg.max_lag_usec) {
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
	rand_seed_thread();

	uint64_t count = 0;

	while (g_running) {
		if (atomic32_incr(&g_reqs_queued) > g_icfg.max_reqs_queued) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t queue_index = count % g_icfg.num_queues;
		uint32_t random_dev_index = rand_32() % g_icfg.num_devices;
		device* random_dev = &g_devices[random_dev_index];

		trans_req rw_req = {
				.dev = random_dev,
				.offset = random_io_offset(random_dev),
				.start_time = get_ns()
		};

		queue_push(g_trans_qs[queue_index], &rw_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / g_icfg.trans_thread_reads_per_sec) -
						(get_us() - g_run_start_us));

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
run_transactions(void* pv_req_q)
{
	queue* req_q = (queue*)pv_req_q;
	trans_req req;

	while (g_running) {
		if (queue_pop(req_q, (void*)&req, 100) != QUEUE_OK) {
			continue;
		}

		uint8_t stack_buffer[IO_SIZE + 4096];
		uint8_t* buf = align_4096(stack_buffer);

		read_and_report(&req, buf);

		atomic32_decr(&g_reqs_queued);
	}

	return NULL;
}


//==========================================================
// Local helpers - generic.
//

//------------------------------------------------
// Discover device storage capacity, etc.
//
static bool
discover_device(device* dev)
{
	int fd = fd_get(dev);

	if (fd == -1) {
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	fd_put(dev, fd);

	if (device_bytes == 0) {
		fprintf(stdout, "ERROR: %s ioctl to discover size\n", dev->name);
		return false;
	}

	fprintf(stdout, "%s size = %" PRIu64 " bytes\n", dev->name, device_bytes);

	dev->n_io_offsets = device_bytes / IO_SIZE;

	return true;
}

//------------------------------------------------
// Close all file descriptors for a device.
//
static void
fd_close_all(device* dev)
{
	int fd;

	while (queue_pop(dev->fd_q, (void*)&fd, QUEUE_NO_WAIT) == QUEUE_OK) {
		close(fd);
	}
}

//------------------------------------------------
// Get a safe file descriptor for a device.
//
static int
fd_get(device* dev)
{
	int fd = -1;

	if (queue_pop(dev->fd_q, (void*)&fd, QUEUE_NO_WAIT) != QUEUE_OK) {
		fd = open(dev->name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);

		if (fd == -1) {
			fprintf(stdout, "ERROR: open device %s errno %d '%s'\n", dev->name,
					errno, strerror(errno));
		}
	}

	return fd;
}

//------------------------------------------------
// Recycle a safe file descriptor for a device.
//
static void
fd_put(device* dev, int fd)
{
	queue_push(dev->fd_q, (void*)&fd);
}

//------------------------------------------------
// Do one transaction read operation and report.
//
static void
read_and_report(trans_req* read_req, uint8_t* buf)
{
	uint64_t raw_start_time = get_ns();
	uint64_t stop_time = read_from_device(read_req->dev, read_req->offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_trans_read_hist,
				safe_delta_ns(read_req->start_time, stop_time));
		histogram_insert_data_point(read_req->dev->raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one cache thread read operation and report.
//
static void
read_cache_and_report(uint8_t* buf)
{
	uint32_t random_device_index = rand_32() % g_icfg.num_devices;
	device* p_device = &g_devices[random_device_index];
	uint64_t offset = random_io_offset(p_device);

	uint64_t raw_start_time = get_ns();
	uint64_t stop_time = read_from_device(p_device, offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(p_device->raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device read operation.
//
static uint64_t
read_from_device(device* dev, uint64_t offset, uint8_t* buf)
{
	int fd = fd_get(dev);

	if (fd == -1) {
		return -1;
	}

	if (pread(fd, buf, IO_SIZE, offset) != IO_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: reading %s: %d '%s'\n", dev->name, errno,
				strerror(errno));
		return -1;
	}

	uint64_t stop_ns = get_ns();

	fd_put(dev, fd);

	return stop_ns;
}

//------------------------------------------------
// Do one cache thread write operation and report.
//
static void
write_cache_and_report(uint8_t* buf)
{
	// Salt the buffer each time.
	rand_fill(buf, IO_SIZE);

	uint32_t random_device_index = rand_32() % g_icfg.num_devices;
	device* p_device = &g_devices[random_device_index];
	uint64_t offset = random_io_offset(p_device);

	uint64_t raw_start_time = get_ns();
	uint64_t stop_time = write_to_device(p_device, offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_raw_write_hist,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(p_device->raw_write_hist,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t
write_to_device(device* dev, uint64_t offset, const uint8_t* buf)
{
	int fd = fd_get(dev);

	if (fd == -1) {
		return -1;
	}

	if (pwrite(fd, buf, IO_SIZE, offset) != IO_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: writing %s: %d '%s'\n", dev->name, errno,
				strerror(errno));
		return -1;
	}

	uint64_t stop_ns = get_ns();

	fd_put(dev, fd);

	return stop_ns;
}
