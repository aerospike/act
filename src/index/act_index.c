/*
 * act_index.c
 *
 * Aerospike Index Certifiction Tool - Simulates and validates primary index
 * SSDs for real-time database use.
 *
 * Kevin Porter & Andrew Gooding, 2018.
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
#include "common/io.h"
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
	histogram* read_hist;
	histogram* write_hist;
	char read_hist_tag[MAX_DEVICE_NAME_SIZE + 1 + 5];
	char write_hist_tag[MAX_DEVICE_NAME_SIZE + 1 + 6];
} device;

typedef struct trans_req_s {
	device* dev;
	uint64_t offset;
} trans_req;

#define IO_SIZE 4096
#define BUNDLE_SIZE 100


//==========================================================
// Forward declarations.
//

static void* run_cache_simulation(void* pv_unused);
static void* run_service(void* pv_unused);

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

static volatile bool g_running;
static uint64_t g_run_start_us;

static histogram* g_read_hist;
static histogram* g_write_hist;


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

	printf("\nACT version %s\n", VERSION);
	printf("Index device IO test\n");
	printf("Copyright 2020 by Aerospike. All rights reserved.\n\n");

	if (! index_configure(argc, argv)) {
		exit(-1);
	}

	device devices[g_icfg.num_devices];

	g_devices = devices;

	histogram_scale scale =
			g_icfg.us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	if (! (g_read_hist = histogram_create(scale)) ||
		! (g_write_hist = histogram_create(scale))) {
		exit(-1);
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* dev = &g_devices[d];

		dev->name = (const char*)g_icfg.device_names[d];

		if (g_icfg.file_size == 0) { // normally 0
			set_scheduler(dev->name, g_icfg.scheduler_mode);
		}

		if (! (dev->fd_q = queue_create(sizeof(int))) ||
			! discover_device(dev) ||
			! (dev->read_hist = histogram_create(scale)) ||
			! (dev->write_hist = histogram_create(scale))) {
			exit(-1);
		}

		sprintf(dev->read_hist_tag, "%s-reads", dev->name);
		sprintf(dev->write_hist_tag, "%s-writes", dev->name);
	}

	rand_seed();

	g_run_start_us = get_us();

	uint64_t run_stop_us = g_run_start_us + g_icfg.run_us;

	g_running = true;

	pthread_t cache_tids[g_icfg.cache_threads];
	bool has_write_load = g_icfg.cache_thread_reads_and_writes_per_sec != 0;

	if (has_write_load) {
		for (uint32_t n = 0; n < g_icfg.cache_threads; n++) {
			if (pthread_create(&cache_tids[n], NULL, run_cache_simulation,
					NULL) != 0) {
				printf("ERROR: create cache thread\n");
				exit(-1);
			}
		}
	}

	pthread_t svc_tids[g_icfg.service_threads];

	for (uint32_t k = 0; k < g_icfg.service_threads; k++) {
		if (pthread_create(&svc_tids[k], NULL, run_service, NULL) != 0) {
			printf("ERROR: create service thread\n");
			exit(-1);
		}
	}

	printf("\nHISTOGRAM NAMES\n");

	printf("reads\n");

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		printf("%s\n", g_devices[d].read_hist_tag);
	}

	if (has_write_load) {
		printf("writes\n");

		for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
			printf("%s\n", g_devices[d].write_hist_tag);
		}
	}

	printf("\n");

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

		printf("after %" PRIu64 " sec:\n",
				(count * g_icfg.report_interval_us) / 1000000);

		histogram_dump(g_read_hist, "reads");

		for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
			histogram_dump(g_devices[d].read_hist,
					g_devices[d].read_hist_tag);
		}

		if (has_write_load) {
			histogram_dump(g_write_hist, "writes");

			for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
				histogram_dump(g_devices[d].write_hist,
						g_devices[d].write_hist_tag);
			}
		}

		printf("\n");
		fflush(stdout);
	}

	g_running = false;

	for (uint32_t k = 0; k < g_icfg.service_threads; k++) {
		pthread_join(svc_tids[k], NULL);
	}

	if (has_write_load) {
		for (uint32_t n = 0; n < g_icfg.cache_threads; n++) {
			pthread_join(cache_tids[n], NULL);
		}
	}

	for (uint32_t d = 0; d < g_icfg.num_devices; d++) {
		device* dev = &g_devices[d];

		fd_close_all(dev);
		queue_destroy(dev->fd_q);
		free(dev->read_hist);
		free(dev->write_hist);
	}

	free(g_read_hist);
	free(g_write_hist);

	return 0;
}


//==========================================================
// Local helpers - thread "run" functions.
//

//------------------------------------------------
// Runs in every (mmap) cache simulation thread,
// does all writes, and reads that don't occur in
// service threads, i.e. reads due to defrag.
//
static void*
run_cache_simulation(void* pv_unused)
{
	rand_seed_thread();

	uint8_t stack_buffer[IO_SIZE + 4096];
	uint8_t* buf = align_4096(stack_buffer);

	uint64_t target_factor =
			1000000ull * g_icfg.num_devices * g_icfg.cache_threads;

	uint64_t count = 0;

	while (g_running) {
		for (uint32_t i = 0; i < BUNDLE_SIZE; i++) {
			read_cache_and_report(buf);
			write_cache_and_report(buf);
		}

		count += BUNDLE_SIZE;

		// TODO - someday (count * target_factor) may overflow a uint64_t.
		uint64_t target_us = (count * target_factor) /
				g_icfg.cache_thread_reads_and_writes_per_sec;

		int64_t sleep_us = (int64_t)(target_us - (get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (g_icfg.max_lag_usec != 0 &&
				sleep_us < -(int64_t)g_icfg.max_lag_usec) {
			printf("ERROR: cache thread device IO can't keep up\n");
			printf("drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	return NULL;
}

//------------------------------------------------
// Service threads - generate and do device reads
// corresponding to read and write request index
// lookups.
//
static void*
run_service(void* pv_unused)
{
	rand_seed_thread();

	uint64_t count = 0;
	uint64_t reads_per_sec =
			g_icfg.service_thread_reads_per_sec / g_icfg.service_threads;

	while (g_running) {
		uint32_t random_dev_index = rand_32() % g_icfg.num_devices;
		device* random_dev = &g_devices[random_dev_index];

		trans_req read_req = {
				.dev = random_dev,
				.offset = random_io_offset(random_dev)
		};

		uint8_t stack_buffer[IO_SIZE + 4096];
		uint8_t* buf = align_4096(stack_buffer);

		read_and_report(&read_req, buf);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / reads_per_sec) -
						(get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (g_icfg.max_lag_usec != 0 &&
				sleep_us < -(int64_t)g_icfg.max_lag_usec) {
			printf("ERROR: read request generator can't keep up\n");
			printf("ACT can't do requested load - test stopped\n");
			printf("try configuring more 'service-threads'\n");
			g_running = false;
		}
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

	if (g_icfg.file_size == 0) {
		ioctl(fd, BLKGETSIZE64, &device_bytes);
	}
	else { // undocumented file mode
		device_bytes = g_icfg.file_size;

		if (ftruncate(fd, (off_t)device_bytes) != 0) {
			printf("ERROR: ftruncate file %s errno %d '%s'\n", dev->name, errno,
					act_strerror(errno));
			fd_put(dev, fd);
			return false;
		}
	}

	fd_put(dev, fd);

	if (device_bytes == 0) {
		printf("ERROR: %s ioctl to discover size\n", dev->name);
		return false;
	}

	printf("%s size = %" PRIu64 " bytes\n", dev->name, device_bytes);

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

	while (queue_pop(dev->fd_q, (void*)&fd)) {
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

	if (! queue_pop(dev->fd_q, (void*)&fd)) {
		int direct_flags = O_DIRECT | (g_icfg.disable_odsync ? 0 : O_DSYNC);
		int flags = O_RDWR | (g_icfg.file_size == 0 ? direct_flags : O_CREAT);

		fd = open(dev->name, flags, S_IRUSR | S_IWUSR);

		if (fd == -1) {
			printf("ERROR: open device %s errno %d '%s'\n", dev->name, errno,
					act_strerror(errno));
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
	uint64_t start_time = get_ns();
	uint64_t stop_time = read_from_device(read_req->dev, read_req->offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_read_hist,
				safe_delta_ns(start_time, stop_time));
		histogram_insert_data_point(read_req->dev->read_hist,
				safe_delta_ns(start_time, stop_time));
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

	uint64_t start_time = get_ns();
	uint64_t stop_time = read_from_device(p_device, offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_read_hist,
				safe_delta_ns(start_time, stop_time));
		histogram_insert_data_point(p_device->read_hist,
				safe_delta_ns(start_time, stop_time));
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

	if (! pread_all(fd, buf, IO_SIZE, offset)) {
		close(fd);
		printf("ERROR: reading %s: %d '%s'\n", dev->name, errno,
				act_strerror(errno));
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
	rand_fill(buf, IO_SIZE, 100);

	uint32_t random_device_index = rand_32() % g_icfg.num_devices;
	device* p_device = &g_devices[random_device_index];
	uint64_t offset = random_io_offset(p_device);

	uint64_t start_time = get_ns();
	uint64_t stop_time = write_to_device(p_device, offset, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_write_hist,
				safe_delta_ns(start_time, stop_time));
		histogram_insert_data_point(p_device->write_hist,
				safe_delta_ns(start_time, stop_time));
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

	if (! pwrite_all(fd, buf, IO_SIZE, offset)) {
		close(fd);
		printf("ERROR: writing %s: %d '%s'\n", dev->name, errno,
				act_strerror(errno));
		return -1;
	}

	uint64_t stop_ns = get_ns();

	fd_put(dev, fd);

	return stop_ns;
}
