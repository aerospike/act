/*
 * act.c
 *
 * Aerospike Certifiction Tool - Simulates and validates SSDs for real-time
 * database use.
 *
 * Joey Shurtleff & Andrew Gooding, 2011.
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

#include "atomic.h"
#include "clock.h"
#include "configuration.h"
#include "histogram.h"
#include "queue.h"
#include "random.h"


//==========================================================
// Typedefs & constants.
//

const char VERSION[] = "4.0";

typedef struct _device {
	const char* name;
	uint32_t n;
	uint64_t num_large_blocks;
	uint64_t num_read_offsets;
	uint64_t num_write_offsets;
	uint32_t min_op_bytes;
	uint32_t min_commit_bytes;
	uint32_t read_bytes;
	uint32_t write_bytes;
	uint32_t num_read_sizes;
	uint32_t num_write_sizes;
	cf_queue* p_fd_queue;
	pthread_t large_block_read_thread;
	pthread_t large_block_write_thread;
	pthread_t tomb_raider_thread;
	histogram* p_raw_read_histogram;
	histogram* p_raw_write_histogram;
	char histogram_tag[MAX_DEVICE_NAME_SIZE];
} device;

typedef struct _trans_req {
	device* p_device;
	uint64_t offset;
	uint32_t size;
	bool is_write;
	uint64_t start_time;
} trans_req;

typedef struct _transq {
	cf_queue* p_req_queue;
	pthread_t* threads;
} transq;

const uint32_t LO_IO_MIN_SIZE = 512;
const uint32_t HI_IO_MIN_SIZE = 4096;
const uint32_t MAX_REQS_QUEUED = 100000;
const int64_t MAX_SLEEP_LAG_USEC = 1000000 * 10;
const uint64_t STAGGER = 1000;
const uint64_t RW_STAGGER = 1000 / 2;

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif


//==========================================================
// Forward declarations.
//

static void* run_generate_read_reqs(void* pv_unused);
static void* run_generate_write_reqs(void* pv_unused);
static void* run_large_block_reads(void* pv_device);
static void* run_large_block_writes(void* pv_device);
static void* run_tomb_raider(void* pv_req_queue);
static void* run_transactions(void* pv_req_queue);

static inline uint8_t* align_4096(const uint8_t* stack_buffer);
static inline uint8_t* cf_valloc(size_t size);
static bool discover_device(device* p_device);
static uint64_t discover_min_op_bytes(int fd, const char* name);
static void discover_read_pattern(device* p_device);
static void discover_write_pattern(device* p_device);
static void fd_close_all(device* p_device);
static int fd_get(device* p_device);
static void fd_put(device* p_device, int fd);
static inline uint32_t rand_31();
static inline uint64_t rand_48();
static inline uint64_t random_large_block_offset(const device* p_device);
static inline uint64_t random_read_offset(const device* p_device);
static inline uint32_t random_read_size(const device* p_device);
static inline uint64_t random_write_offset(const device* p_device);
static inline uint32_t random_write_size(const device* p_device);
static void read_and_report(trans_req* p_read_req, uint8_t* p_buffer);
static void read_and_report_large_block(device* p_device, uint8_t* p_buffer);
static uint64_t read_from_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer);
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns);
static void set_schedulers();
static void write_and_report(trans_req* p_write_req, uint8_t* p_buffer);
static void write_and_report_large_block(device* p_device, uint8_t* p_buffer,
		uint64_t count);
static uint64_t write_to_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer);

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

static histogram* g_p_large_block_read_histogram;
static histogram* g_p_large_block_write_histogram;
static histogram* g_p_raw_read_histogram;
static histogram* g_p_read_histogram;
static histogram* g_p_raw_write_histogram;
static histogram* g_p_write_histogram;


//==========================================================
// Main.
//

int
main(int argc, char* argv[])
{
	signal(SIGSEGV, as_sig_handle_segv);
	signal(SIGTERM , as_sig_handle_term);

	fprintf(stdout, "\nAerospike act version %s - device IO test\n", VERSION);
	fprintf(stdout, "Copyright 2011 by Aerospike. All rights reserved.\n\n");

	if (! configure(argc, argv)) {
		exit(-1);
	}

	set_schedulers();

	srand(time(NULL));

	if (! rand_seed()) {
		exit(-1);
	}

	device devices[g_cfg.num_devices];
	transq transqs[g_cfg.num_queues];

	g_devices = devices;
	g_transqs = transqs;

	histogram_scale scale =
			g_cfg.us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	if (! (g_p_large_block_read_histogram = histogram_create(scale)) ||
		! (g_p_large_block_write_histogram = histogram_create(scale)) ||
		! (g_p_raw_read_histogram = histogram_create(scale)) ||
		! (g_p_read_histogram = histogram_create(scale))) {
		exit(-1);
	}

	bool do_commits = g_cfg.commit_to_device && g_cfg.write_reqs_per_sec != 0;

	if (do_commits &&
		(! (g_p_raw_write_histogram = histogram_create(scale)) ||
		 ! (g_p_write_histogram = histogram_create(scale)))) {
		exit(-1);
	}

	for (uint32_t n = 0; n < g_cfg.num_devices; n++) {
		device* p_device = &g_devices[n];

		p_device->name = g_cfg.device_names[n];
		p_device->n = n;

		if (! (p_device->p_fd_queue = cf_queue_create(sizeof(int), true)) ||
			! discover_device(p_device) ||
			! (p_device->p_raw_read_histogram = histogram_create(scale))) {
			exit(-1);
		}

		if (do_commits &&
			! (p_device->p_raw_write_histogram = histogram_create(scale))) {
			exit(-1);
		}

		sprintf(p_device->histogram_tag, "%-18s", p_device->name);
	}

	usleep((g_cfg.num_devices + 1) * STAGGER); // stagger large block ops
	g_run_start_us = cf_getus();

	uint64_t run_stop_us = g_run_start_us + g_cfg.run_us;

	g_running = true;

	if (g_cfg.write_reqs_per_sec != 0) {
		// Separate loops help writer threads start on different cores.
		for (uint32_t n = 0; n < g_cfg.num_devices; n++) {
			device* p_device = &g_devices[n];

			if (pthread_create(&p_device->large_block_write_thread, NULL,
					run_large_block_writes, (void*)p_device) != 0) {
				fprintf(stdout, "ERROR: create large op write thread\n");
				exit(-1);
			}
		}

		for (uint32_t n = 0; n < g_cfg.num_devices; n++) {
			device* p_device = &g_devices[n];

			if (pthread_create(&p_device->large_block_read_thread, NULL,
					run_large_block_reads, (void*)p_device) != 0) {
				fprintf(stdout, "ERROR: create large op read thread\n");
				exit(-1);
			}
		}
	}

	if (g_cfg.tomb_raider) {
		for (uint32_t n = 0; n < g_cfg.num_devices; n++) {
			device* p_device = &g_devices[n];

			if (pthread_create(&p_device->tomb_raider_thread, NULL,
					run_tomb_raider, (void*)p_device) != 0) {
				fprintf(stdout, "ERROR: create tomb raider thread\n");
				exit(-1);
			}
		}
	}

	for (uint32_t i = 0; i < g_cfg.num_queues; i++) {
		transq* p_transq = &g_transqs[i];

		if (! (p_transq->p_req_queue =
				cf_queue_create(sizeof(trans_req), true))) {
			exit(-1);
		}

		if (! (p_transq->threads =
				malloc(sizeof(pthread_t) * g_cfg.threads_per_queue))) {
			fprintf(stdout, "ERROR: malloc transaction threads array\n");
			exit(-1);
		}

		for (uint32_t j = 0; j < g_cfg.threads_per_queue; j++) {
			if (pthread_create(&p_transq->threads[j], NULL, run_transactions,
					(void*)p_transq->p_req_queue) != 0) {
				fprintf(stdout, "ERROR: create transaction thread\n");
				exit(-1);
			}
		}
	}

	pthread_t read_req_generator;

	if (pthread_create(&read_req_generator, NULL, run_generate_read_reqs,
			NULL) != 0) {
		fprintf(stdout, "ERROR: create read request generator thread\n");
		exit(-1);
	}

	pthread_t write_req_generator;

	if (do_commits &&
			pthread_create(&write_req_generator, NULL, run_generate_write_reqs,
					NULL) != 0) {
		fprintf(stdout, "ERROR: create write request generator thread\n");
		exit(-1);
	}

	fprintf(stdout, "\n");

	uint64_t now_us = 0;
	uint64_t count = 0;

	while (g_running && (now_us = cf_getus()) < run_stop_us) {
		count++;

		int64_t sleep_us = (int64_t)
				((count * g_cfg.report_interval_us) -
						(now_us - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}

		fprintf(stdout, "After %" PRIu64 " sec:\n",
				(count * g_cfg.report_interval_us) / 1000000);

		fprintf(stdout, "requests queued: %" PRIu32 "\n",
				cf_atomic32_get(g_reqs_queued));

		histogram_dump(g_p_large_block_read_histogram,  "LARGE BLOCK READS ");
		histogram_dump(g_p_large_block_write_histogram, "LARGE BLOCK WRITES");
		histogram_dump(g_p_raw_read_histogram,          "RAW READS         ");

		for (uint32_t d = 0; d < g_cfg.num_devices; d++) {
			histogram_dump(g_devices[d].p_raw_read_histogram,
					g_devices[d].histogram_tag);
		}

		histogram_dump(g_p_read_histogram,              "READS             ");

		if (do_commits) {
			histogram_dump(g_p_raw_write_histogram,     "RAW WRITES        ");

			for (uint32_t d = 0; d < g_cfg.num_devices; d++) {
				histogram_dump(g_devices[d].p_raw_write_histogram,
						g_devices[d].histogram_tag);
			}

			histogram_dump(g_p_write_histogram,         "WRITES            ");
		}

		fprintf(stdout, "\n");
		fflush(stdout);
	}

	g_running = false;

	void* pv_value;

	pthread_join(read_req_generator, &pv_value);

	if (do_commits) {
		pthread_join(write_req_generator, &pv_value);
	}

	for (uint32_t i = 0; i < g_cfg.num_queues; i++) {
		transq* p_transq = &g_transqs[i];

		for (uint32_t j = 0; j < g_cfg.threads_per_queue; j++) {
			pthread_join(p_transq->threads[j], &pv_value);
		}

		cf_queue_destroy(p_transq->p_req_queue);
		free(p_transq->threads);
	}

	for (uint32_t d = 0; d < g_cfg.num_devices; d++) {
		device* p_device = &g_devices[d];

		if (g_cfg.tomb_raider) {
			pthread_join(p_device->tomb_raider_thread, &pv_value);
		}

		if (g_cfg.write_reqs_per_sec != 0) {
			pthread_join(p_device->large_block_read_thread, &pv_value);
			pthread_join(p_device->large_block_write_thread, &pv_value);
		}

		fd_close_all(p_device);
		cf_queue_destroy(p_device->p_fd_queue);
		free(p_device->p_raw_read_histogram);

		if (do_commits) {
			free(p_device->p_raw_write_histogram);
		}
	}

	free(g_p_large_block_read_histogram);
	free(g_p_large_block_write_histogram);
	free(g_p_raw_read_histogram);
	free(g_p_read_histogram);

	if (do_commits) {
		free(g_p_raw_write_histogram);
		free(g_p_write_histogram);
	}

	return 0;
}


//==========================================================
// Helpers - thread "run" functions.
//

//------------------------------------------------
// Runs in single thread, adds read trans_req
// objects to transaction queues in round-robin
// fashion.
//
static void*
run_generate_read_reqs(void* pv_unused)
{
	uint64_t count = 0;

	while (g_running) {
		if (cf_atomic32_incr(&g_reqs_queued) > MAX_REQS_QUEUED) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t queue_index = count % g_cfg.num_queues;
		uint32_t random_device_index = rand_31() % g_cfg.num_devices;
		device* p_random_device = &g_devices[random_device_index];

		trans_req read_req = {
				.p_device = p_random_device,
				.offset = random_read_offset(p_random_device),
				.size = random_read_size(p_random_device),
				.is_write = false,
				.start_time = cf_getns()
		};

		cf_queue_push(g_transqs[queue_index].p_req_queue, &read_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / g_cfg.internal_read_reqs_per_sec) -
						(cf_getus() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in single thread, adds write trans_req
// objects to transaction queues in round-robin
// fashion.
//
static void*
run_generate_write_reqs(void* pv_unused)
{
	uint64_t count = 0;

	while (g_running) {
		if (cf_atomic32_incr(&g_reqs_queued) > MAX_REQS_QUEUED) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t queue_index = count % g_cfg.num_queues;
		uint32_t random_device_index = rand_31() % g_cfg.num_devices;
		device* p_random_device = &g_devices[random_device_index];

		trans_req write_req = {
				.p_device = p_random_device,
				.offset = random_write_offset(p_random_device),
				.size = random_write_size(p_random_device),
				.is_write = true,
				.start_time = cf_getns()
		};

		cf_queue_push(g_transqs[queue_index].p_req_queue, &write_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / g_cfg.internal_write_reqs_per_sec) -
						(cf_getus() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block read thread,
// executes large-block reads at a constant rate.
//
static void*
run_large_block_reads(void* pv_device)
{
	device* p_device = (device*)pv_device;

	uint8_t* p_buffer = cf_valloc(g_cfg.large_block_ops_bytes);

	if (! p_buffer) {
		fprintf(stdout, "ERROR: large block read buffer cf_valloc()\n");
		return NULL;
	}

	uint64_t start_us = g_run_start_us - (p_device->n * STAGGER);
	uint64_t count = 0;

	while (g_running) {
		read_and_report_large_block(p_device, p_buffer);

		count++;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_cfg.num_devices) /
						g_cfg.large_block_reads_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (cf_getus() - start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -MAX_SLEEP_LAG_USEC) {
			fprintf(stdout, "ERROR: large block reads can't keep up\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	free(p_buffer);

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block write thread,
// executes large-block writes at a constant rate.
//
static void*
run_large_block_writes(void* pv_device)
{
	device* p_device = (device*)pv_device;

	uint8_t* p_buffer = cf_valloc(g_cfg.large_block_ops_bytes);

	if (! p_buffer) {
		fprintf(stdout, "ERROR: large block write buffer cf_valloc()\n");
		return NULL;
	}

	uint64_t start_us = g_run_start_us - (p_device->n * STAGGER) - RW_STAGGER;
	uint64_t count = 0;

	while (g_running) {
		write_and_report_large_block(p_device, p_buffer, count);

		count++;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_cfg.num_devices) /
						g_cfg.large_block_writes_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (cf_getus() - start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -MAX_SLEEP_LAG_USEC) {
			fprintf(stdout, "ERROR: large block writes can't keep up\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	free(p_buffer);

	return NULL;
}

//------------------------------------------------
// Runs in every device tomb raider thread,
// executes continuous large-block reads.
//
static void*
run_tomb_raider(void* pv_device)
{
	device* p_device = (device*)pv_device;

	uint8_t* p_buffer = cf_valloc(g_cfg.large_block_ops_bytes);

	if (! p_buffer) {
		fprintf(stdout, "ERROR: tomb raider buffer cf_valloc()\n");
		return NULL;
	}

	uint64_t offset = 0;
	uint64_t end = p_device->num_large_blocks * g_cfg.large_block_ops_bytes;

	while (g_running) {
		if (g_cfg.tomb_raider_sleep_us != 0) {
			usleep(g_cfg.tomb_raider_sleep_us);
		}

		read_from_device(p_device, offset, g_cfg.large_block_ops_bytes,
				p_buffer);

		offset += g_cfg.large_block_ops_bytes;

		if (offset == end) {
			offset = 0;
		}
	}

	free(p_buffer);

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

		uint8_t stack_buffer[req.size + 4096];
		uint8_t* p_buffer = align_4096(stack_buffer);

		if (req.is_write) {
			write_and_report(&req, p_buffer);
		}
		else {
			read_and_report(&req, p_buffer);
		}

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
// Aligned memory allocation.
//
static inline uint8_t*
cf_valloc(size_t size)
{
	void* pv;

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
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
	p_device->num_large_blocks = device_bytes / g_cfg.large_block_ops_bytes;
	p_device->min_op_bytes = discover_min_op_bytes(fd, p_device->name);
	fd_put(p_device, fd);

	if (p_device->num_large_blocks == 0) {
		fprintf(stdout, "ERROR: %s ioctl to discover size\n", p_device->name);
		return false;
	}

	if (p_device->min_op_bytes == 0) {
		return false;
	}

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks, "
			"minimum IO size = %" PRIu32 " bytes\n",
			p_device->name, device_bytes, p_device->num_large_blocks,
			p_device->min_op_bytes);

	discover_read_pattern(p_device);

	if (g_cfg.commit_to_device) {
		discover_write_pattern(p_device);
	}
	// else - write load is all accounted for with large-block writes.

	return true;
}

//------------------------------------------------
// Discover device's minimum direct IO op size.
//
static uint64_t
discover_min_op_bytes(int fd, const char* name)
{
	off_t off = lseek(fd, 0, SEEK_SET);

	if (off != 0) {
		fprintf(stdout, "ERROR: %s seek errno %d '%s'\n", name, errno,
				strerror(errno));
		return 0;
	}

	uint8_t* buf = cf_valloc(HI_IO_MIN_SIZE);

	if (! buf) {
		fprintf(stdout, "ERROR: IO min size buffer cf_valloc()\n");
		return 0;
	}

	size_t read_sz = LO_IO_MIN_SIZE;

	while (read_sz <= HI_IO_MIN_SIZE) {
		if (read(fd, (void*)buf, read_sz) == (ssize_t)read_sz) {
			free(buf);
			return read_sz;
		}

		read_sz <<= 1; // LO_IO_MIN_SIZE and HI_IO_MIN_SIZE are powers of 2
	}

	fprintf(stdout, "ERROR: %s read failed at all sizes from %u to %u bytes\n",
			name, LO_IO_MIN_SIZE, HI_IO_MIN_SIZE);

	free(buf);

	return 0;
}

//------------------------------------------------
// Discover device's read request pattern.
//
static void
discover_read_pattern(device* p_device)
{
	// Total number of "min-op"-sized blocks on the device. (Excluding
	// fractional large block at end of device, if such.)
	uint64_t num_min_op_blocks =
			(p_device->num_large_blocks * g_cfg.large_block_ops_bytes) /
					p_device->min_op_bytes;

	// Number of "min-op"-sized blocks per (smallest) read request.
	uint32_t read_req_min_op_blocks =
			(g_cfg.record_stored_bytes + p_device->min_op_bytes - 1) /
					p_device->min_op_bytes;

	// Size in bytes per (smallest) read request.
	p_device->read_bytes =
			read_req_min_op_blocks * p_device->min_op_bytes;

	// Number of "min-op"-sized blocks per (largest) read request.
	uint32_t read_req_min_op_blocks_rmx =
			(g_cfg.record_stored_bytes_rmx + p_device->min_op_bytes - 1) /
					p_device->min_op_bytes;

	// Number of read request sizes in configured range.
	p_device->num_read_sizes =
			read_req_min_op_blocks_rmx - read_req_min_op_blocks + 1;

	// Total number of sites on device to read from. (Make sure the last site
	// has room for largest possible read request.)
	p_device->num_read_offsets =
			num_min_op_blocks - read_req_min_op_blocks_rmx + 1;
}

//------------------------------------------------
// Discover device's write request pattern.
//
static void
discover_write_pattern(device* p_device)
{
	// Use the larger of min-op bytes and configured commit-min-bytes.
	p_device->min_commit_bytes =
			p_device->min_op_bytes > g_cfg.commit_min_bytes ?
					p_device->min_op_bytes : g_cfg.commit_min_bytes;

	// Total number of "min-commit"-sized blocks on the device. (Excluding
	// fractional large block at end of device, if such.)
	uint64_t num_min_commit_blocks =
			(p_device->num_large_blocks * g_cfg.large_block_ops_bytes) /
					p_device->min_commit_bytes;

	// Number of "min-commit"-sized blocks per (smallest) write request.
	uint32_t write_req_min_commit_blocks =
			(g_cfg.record_stored_bytes + p_device->min_commit_bytes - 1) /
					p_device->min_commit_bytes;

	// Size in bytes per (smallest) write request.
	p_device->write_bytes =
			write_req_min_commit_blocks * p_device->min_commit_bytes;

	// Number of "min-commit"-sized blocks per (largest) write request.
	uint32_t write_req_min_commit_blocks_rmx =
			(g_cfg.record_stored_bytes_rmx + p_device->min_commit_bytes - 1) /
					p_device->min_commit_bytes;

	// Number of write request sizes in configured range.
	p_device->num_write_sizes =
			write_req_min_commit_blocks_rmx - write_req_min_commit_blocks + 1;

	// Total number of sites on device to write to. (Make sure the last site
	// has room for largest possible write request.)
	p_device->num_write_offsets =
			num_min_commit_blocks - write_req_min_commit_blocks_rmx + 1;
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

	return (fd);
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
// Get a random large block offset for a device.
//
static inline uint64_t
random_large_block_offset(const device* p_device)
{
	return (rand_48() % p_device->num_large_blocks) *
			g_cfg.large_block_ops_bytes;
}

//------------------------------------------------
// Get a random read offset for a device.
//
static inline uint64_t
random_read_offset(const device* p_device)
{
	return (rand_48() % p_device->num_read_offsets) * p_device->min_op_bytes;
}

//------------------------------------------------
// Get a random read size for a device.
//
static inline uint32_t
random_read_size(const device* p_device)
{
	if (p_device->num_read_sizes == 1) {
		return p_device->read_bytes;
	}

	return p_device->read_bytes +
			(p_device->min_op_bytes * (rand_31() % p_device->num_read_sizes));
}

//------------------------------------------------
// Get a random write offset for a device.
//
static inline uint64_t
random_write_offset(const device* p_device)
{
	return (rand_48() % p_device->num_write_offsets) *
			p_device->min_commit_bytes;
}

//------------------------------------------------
// Get a random write size for a device.
//
static inline uint32_t
random_write_size(const device* p_device)
{
	if (p_device->num_write_sizes == 1) {
		return p_device->write_bytes;
	}

	return p_device->write_bytes +
			(p_device->min_commit_bytes *
					(rand_31() % p_device->num_write_sizes));
}

//------------------------------------------------
// Do one transaction read operation and report.
//
static void
read_and_report(trans_req* p_read_req, uint8_t* p_buffer)
{
	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_read_req->p_device,
			p_read_req->offset, p_read_req->size, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_p_read_histogram,
				safe_delta_ns(p_read_req->start_time, stop_time));
		histogram_insert_data_point(p_read_req->p_device->p_raw_read_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one large block read operation and report.
//
static void
read_and_report_large_block(device* p_device, uint8_t* p_buffer)
{
	uint64_t offset = random_large_block_offset(p_device);
	uint64_t start_time = cf_getns();
	uint64_t stop_time = read_from_device(p_device, offset,
			g_cfg.large_block_ops_bytes, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_large_block_read_histogram,
				safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device read operation.
//
static uint64_t
read_from_device(device* p_device, uint64_t offset, uint32_t size,
		uint8_t* p_buffer)
{
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, size) != (ssize_t)size) {
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
	const char* mode = SCHEDULER_MODES[g_cfg.scheduler_mode];
	size_t mode_length = strlen(mode);

	for (uint32_t d = 0; d < g_cfg.num_devices; d++) {
		const char* device_name = g_cfg.device_names[d];
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
// Do one transaction write operation and report.
//
static void
write_and_report(trans_req* p_write_req, uint8_t* p_buffer)
{
	uint64_t raw_start_time = cf_getns();
	uint64_t stop_time = write_to_device(p_write_req->p_device,
			p_write_req->offset, p_write_req->size, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_raw_write_histogram,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_p_write_histogram,
				safe_delta_ns(p_write_req->start_time, stop_time));
		histogram_insert_data_point(
				p_write_req->p_device->p_raw_write_histogram,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one large block write operation and report.
//
static void
write_and_report_large_block(device* p_device, uint8_t* p_buffer,
		uint64_t count)
{
	// Salt the block each time.
	rand_fill(p_buffer, g_cfg.large_block_ops_bytes);

	uint64_t offset = random_large_block_offset(p_device);
	uint64_t start_time = cf_getns();
	uint64_t stop_time = write_to_device(p_device, offset,
			g_cfg.large_block_ops_bytes, p_buffer);

	if (stop_time != -1) {
		histogram_insert_data_point(g_p_large_block_write_histogram,
				safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t
write_to_device(device* p_device, uint64_t offset, uint32_t size,
		uint8_t* p_buffer)
{
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			write(fd, p_buffer, size) != (ssize_t)size) {
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
