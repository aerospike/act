/*
 * act_storage.c
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

#include "cfg_storage.h"


//==========================================================
// Typedefs & constants.
//

typedef struct device_s {
	const char* name;
	uint64_t n_large_blocks;
	uint64_t n_read_offsets;
	uint64_t n_write_offsets;
	uint32_t min_op_bytes;
	uint32_t min_commit_bytes;
	uint32_t read_bytes;
	uint32_t write_bytes;
	uint32_t n_read_sizes;
	uint32_t n_write_sizes;
	queue* fd_q;
	pthread_t large_block_read_thread;
	pthread_t large_block_write_thread;
	pthread_t tomb_raider_thread;
	histogram* raw_read_hist;
	histogram* raw_write_hist;
	char read_hist_tag[MAX_DEVICE_NAME_SIZE + 1 + 5];
	char write_hist_tag[MAX_DEVICE_NAME_SIZE + 1 + 6];
} device;

typedef struct trans_req_s {
	device* dev;
	uint64_t offset;
	uint32_t size;
	bool is_write;
	uint64_t start_time;
} trans_req;

#define LO_IO_MIN_SIZE 512
#define HI_IO_MIN_SIZE 4096


//==========================================================
// Forward declarations.
//

static void* run_generate_read_reqs(void* pv_unused);
static void* run_generate_write_reqs(void* pv_unused);
static void* run_large_block_reads(void* pv_dev);
static void* run_large_block_writes(void* pv_dev);
static void* run_tomb_raider(void* pv_dev);
static void* run_transactions(void* pv_req_q);

static uint8_t* act_valloc(size_t size);
static bool discover_device(device* dev);
static uint64_t discover_min_op_bytes(int fd, const char* name);
static void discover_read_pattern(device* dev);
static void discover_write_pattern(device* dev);
static void fd_close_all(device* dev);
static int fd_get(device* dev);
static void fd_put(device* dev, int fd);
static void read_and_report(trans_req* read_req, uint8_t* buf);
static void read_and_report_large_block(device* dev, uint8_t* buf);
static uint64_t read_from_device(device* dev, uint64_t offset, uint32_t size,
		uint8_t* buf);
static void write_and_report(trans_req* write_req, uint8_t* buf);
static void write_and_report_large_block(device* dev, uint8_t* buf,
		uint64_t count);
static uint64_t write_to_device(device* dev, uint64_t offset, uint32_t size,
		const uint8_t* buf);


//==========================================================
// Globals.
//

static device* g_devices;
static queue** g_trans_qs;

static volatile bool g_running;
static uint64_t g_run_start_us;

static atomic32 g_reqs_queued = 0;

static histogram* g_large_block_read_hist;
static histogram* g_large_block_write_hist;
static histogram* g_raw_read_hist;
static histogram* g_read_hist;
static histogram* g_raw_write_hist;
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
random_large_block_offset(const device* dev)
{
	return (rand_64() % dev->n_large_blocks) * g_scfg.large_block_ops_bytes;
}

static inline uint64_t
random_read_offset(const device* dev)
{
	return (rand_64() % dev->n_read_offsets) * dev->min_op_bytes;
}

static inline uint32_t
random_read_size(const device* dev)
{
	if (dev->n_read_sizes == 1) {
		return dev->read_bytes;
	}

	return dev->read_bytes +
			(dev->min_op_bytes * (rand_32() % dev->n_read_sizes));
}

static inline uint64_t
random_write_offset(const device* dev)
{
	return (rand_64() % dev->n_write_offsets) * dev->min_commit_bytes;
}

static inline uint32_t
random_write_size(const device* dev)
{
	if (dev->n_write_sizes == 1) {
		return dev->write_bytes;
	}

	return dev->write_bytes +
			(dev->min_commit_bytes * (rand_32() % dev->n_write_sizes));
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

	fprintf(stdout, "\nACT version %s\n", VERSION);
	fprintf(stdout, "Storage device IO test\n");
	fprintf(stdout, "Copyright 2018 by Aerospike. All rights reserved.\n\n");

	if (! storage_configure(argc, argv)) {
		exit(-1);
	}

	device devices[g_scfg.num_devices];
	queue* trans_qs[g_scfg.num_queues];

	g_devices = devices;
	g_trans_qs = trans_qs;

	histogram_scale scale =
			g_scfg.us_histograms ? HIST_MICROSECONDS : HIST_MILLISECONDS;

	if (! (g_large_block_read_hist = histogram_create(scale)) ||
		! (g_large_block_write_hist = histogram_create(scale)) ||
		! (g_raw_read_hist = histogram_create(scale)) ||
		! (g_read_hist = histogram_create(scale)) ||
		! (g_raw_write_hist = histogram_create(scale)) ||
		! (g_write_hist = histogram_create(scale))) {
		exit(-1);
	}

	for (uint32_t n = 0; n < g_scfg.num_devices; n++) {
		device* dev = &g_devices[n];

		dev->name = (const char*)g_scfg.device_names[n];

		if (g_scfg.file_size == 0) { // normally 0
			set_scheduler(dev->name, g_scfg.scheduler_mode);
		}

		if (! (dev->fd_q = queue_create(sizeof(int), true)) ||
			! discover_device(dev) ||
			! (dev->raw_read_hist = histogram_create(scale)) ||
			! (dev->raw_write_hist = histogram_create(scale))) {
			exit(-1);
		}

		sprintf(dev->read_hist_tag, "%s-reads", dev->name);
		sprintf(dev->write_hist_tag, "%s-writes", dev->name);
	}

	rand_seed();

	g_run_start_us = get_us();

	uint64_t run_stop_us = g_run_start_us + g_scfg.run_us;

	g_running = true;

	if (g_scfg.write_reqs_per_sec != 0) {
		for (uint32_t n = 0; n < g_scfg.num_devices; n++) {
			device* dev = &g_devices[n];

			if (pthread_create(&dev->large_block_read_thread, NULL,
					run_large_block_reads, (void*)dev) != 0) {
				fprintf(stdout, "ERROR: create large op read thread\n");
				exit(-1);
			}

			if (pthread_create(&dev->large_block_write_thread, NULL,
					run_large_block_writes, (void*)dev) != 0) {
				fprintf(stdout, "ERROR: create large op write thread\n");
				exit(-1);
			}
		}
	}

	if (g_scfg.tomb_raider) {
		for (uint32_t n = 0; n < g_scfg.num_devices; n++) {
			device* dev = &g_devices[n];

			if (pthread_create(&dev->tomb_raider_thread, NULL,
					run_tomb_raider, (void*)dev) != 0) {
				fprintf(stdout, "ERROR: create tomb raider thread\n");
				exit(-1);
			}
		}
	}

	uint32_t n_trans_tids = g_scfg.num_queues * g_scfg.threads_per_queue;
	pthread_t trans_tids[n_trans_tids];

	for (uint32_t i = 0; i < g_scfg.num_queues; i++) {
		if (! (g_trans_qs[i] = queue_create(sizeof(trans_req), true))) {
			exit(-1);
		}

		for (uint32_t j = 0; j < g_scfg.threads_per_queue; j++) {
			if (pthread_create(&trans_tids[(i * g_scfg.threads_per_queue) + j],
					NULL, run_transactions, (void*)g_trans_qs[i]) != 0) {
				fprintf(stdout, "ERROR: create transaction thread\n");
				exit(-1);
			}
		}
	}

	// Equivalent: g_scfg.internal_read_reqs_per_sec != 0.
	bool do_reads = g_scfg.read_reqs_per_sec != 0;

	pthread_t read_req_tids[g_scfg.read_req_threads];

	if (do_reads) {
		for (uint32_t k = 0; k < g_scfg.read_req_threads; k++) {
			if (pthread_create(&read_req_tids[k], NULL, run_generate_read_reqs,
					NULL) != 0) {
				fprintf(stdout, "ERROR: create read request thread\n");
				exit(-1);
			}
		}
	}

	// Equivalent: g_scfg.internal_write_reqs_per_sec != 0.
	bool do_commits = g_scfg.commit_to_device && g_scfg.write_reqs_per_sec != 0;

	pthread_t write_req_tids[g_scfg.write_req_threads];

	if (do_commits) {
		for (uint32_t k = 0; k < g_scfg.write_req_threads; k++) {
			if (pthread_create(&write_req_tids[k], NULL,
					run_generate_write_reqs, NULL) != 0) {
				fprintf(stdout, "ERROR: create write request thread\n");
				exit(-1);
			}
		}
	}

	fprintf(stdout, "\nHISTOGRAM NAMES\n");

	if (do_reads) {
		fprintf(stdout, "reads\n");
		fprintf(stdout, "device-reads\n");

		for (uint32_t d = 0; d < g_scfg.num_devices; d++) {
			fprintf(stdout, "%s\n", g_devices[d].read_hist_tag);
		}
	}

	if (g_scfg.write_reqs_per_sec != 0) {
		fprintf(stdout, "large-block-reads\n");
		fprintf(stdout, "large-block-writes\n");
	}

	if (do_commits) {
		fprintf(stdout, "writes\n");
		fprintf(stdout, "device-writes\n");

		for (uint32_t d = 0; d < g_scfg.num_devices; d++) {
			fprintf(stdout, "%s\n", g_devices[d].write_hist_tag);
		}
	}

	fprintf(stdout, "\n");

	uint64_t now_us = 0;
	uint64_t count = 0;

	while (g_running && (now_us = get_us()) < run_stop_us) {
		count++;

		int64_t sleep_us = (int64_t)
				((count * g_scfg.report_interval_us) -
						(now_us - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}

		fprintf(stdout, "after %" PRIu64 " sec:\n",
				(count * g_scfg.report_interval_us) / 1000000);

		fprintf(stdout, "requests-queued: %" PRIu32 "\n",
				atomic32_get(g_reqs_queued));

		if (do_reads) {
			histogram_dump(g_read_hist, "reads");
			histogram_dump(g_raw_read_hist, "device-reads");

			for (uint32_t d = 0; d < g_scfg.num_devices; d++) {
				histogram_dump(g_devices[d].raw_read_hist,
						g_devices[d].read_hist_tag);
			}
		}

		if (g_scfg.write_reqs_per_sec != 0) {
			histogram_dump(g_large_block_read_hist, "large-block-reads");
			histogram_dump(g_large_block_write_hist, "large-block-writes");
		}

		if (do_commits) {
			histogram_dump(g_write_hist, "writes");
			histogram_dump(g_raw_write_hist, "device-writes");

			for (uint32_t d = 0; d < g_scfg.num_devices; d++) {
				histogram_dump(g_devices[d].raw_write_hist,
						g_devices[d].write_hist_tag);
			}
		}

		fprintf(stdout, "\n");
		fflush(stdout);
	}

	g_running = false;

	if (do_reads) {
		for (uint32_t k = 0; k < g_scfg.read_req_threads; k++) {
			pthread_join(read_req_tids[k], NULL);
		}
	}

	if (do_commits) {
		for (uint32_t k = 0; k < g_scfg.write_req_threads; k++) {
			pthread_join(write_req_tids[k], NULL);
		}
	}

	for (uint32_t j = 0; j < n_trans_tids; j++) {
		pthread_join(trans_tids[j], NULL);
	}

	for (uint32_t i = 0; i < g_scfg.num_queues; i++) {
		queue_destroy(g_trans_qs[i]);
	}

	for (uint32_t d = 0; d < g_scfg.num_devices; d++) {
		device* dev = &g_devices[d];

		if (g_scfg.tomb_raider) {
			pthread_join(dev->tomb_raider_thread, NULL);
		}

		if (g_scfg.write_reqs_per_sec != 0) {
			pthread_join(dev->large_block_read_thread, NULL);
			pthread_join(dev->large_block_write_thread, NULL);
		}

		fd_close_all(dev);
		queue_destroy(dev->fd_q);
		free(dev->raw_read_hist);
		free(dev->raw_write_hist);
	}

	free(g_large_block_read_hist);
	free(g_large_block_write_hist);
	free(g_raw_read_hist);
	free(g_read_hist);
	free(g_raw_write_hist);
	free(g_write_hist);

	return 0;
}


//==========================================================
// Local helpers - thread "run" functions.
//

//------------------------------------------------
// Runs in service threads, adds read trans_req
// objects to transaction queues in round-robin
// fashion.
//
static void*
run_generate_read_reqs(void* pv_unused)
{
	rand_seed_thread();

	uint64_t count = 0;
	uint64_t internal_read_reqs_per_sec =
			g_scfg.internal_read_reqs_per_sec / g_scfg.read_req_threads;

	while (g_running) {
		if (atomic32_incr(&g_reqs_queued) > g_scfg.max_reqs_queued) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t q_index = count % g_scfg.num_queues;
		uint32_t random_dev_index = rand_32() % g_scfg.num_devices;
		device* random_dev = &g_devices[random_dev_index];

		trans_req read_req = {
				.dev = random_dev,
				.offset = random_read_offset(random_dev),
				.size = random_read_size(random_dev),
				.is_write = false,
				.start_time = get_ns()
		};

		queue_push(g_trans_qs[q_index], &read_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / internal_read_reqs_per_sec) -
						(get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -(int64_t)g_scfg.max_lag_usec) {
			fprintf(stdout, "ERROR: read request generator can't keep up\n");
			fprintf(stdout, "ACT can't do requested load - test stopped\n");
			fprintf(stdout, "try configuring more 'service-threads'\n");
			g_running = false;
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in service threads, adds write trans_req
// objects to transaction queues in round-robin
// fashion.
//
static void*
run_generate_write_reqs(void* pv_unused)
{
	rand_seed_thread();

	uint64_t count = 0;
	uint64_t internal_write_reqs_per_sec =
			g_scfg.internal_write_reqs_per_sec / g_scfg.write_req_threads;

	while (g_running) {
		if (atomic32_incr(&g_reqs_queued) > g_scfg.max_reqs_queued) {
			fprintf(stdout, "ERROR: too many requests queued\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
			break;
		}

		uint32_t q_index = count % g_scfg.num_queues;
		uint32_t random_dev_index = rand_32() % g_scfg.num_devices;
		device* random_dev = &g_devices[random_dev_index];

		trans_req write_req = {
				.dev = random_dev,
				.offset = random_write_offset(random_dev),
				.size = random_write_size(random_dev),
				.is_write = true,
				.start_time = get_ns()
		};

		queue_push(g_trans_qs[q_index], &write_req);

		count++;

		int64_t sleep_us = (int64_t)
				(((count * 1000000) / internal_write_reqs_per_sec) -
						(get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -(int64_t)g_scfg.max_lag_usec) {
			fprintf(stdout, "ERROR: write request generator can't keep up\n");
			fprintf(stdout, "ACT can't do requested load - test stopped\n");
			fprintf(stdout, "try configuring more 'service-threads'\n");
			g_running = false;
		}
	}

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block read thread,
// executes large-block reads at a constant rate.
//
static void*
run_large_block_reads(void* pv_dev)
{
	rand_seed_thread();

	device* dev = (device*)pv_dev;

	uint8_t* buf = act_valloc(g_scfg.large_block_ops_bytes);

	if (! buf) {
		fprintf(stdout, "ERROR: large block read buffer act_valloc()\n");
		g_running = false;
		return NULL;
	}

	uint64_t count = 0;

	while (g_running) {
		read_and_report_large_block(dev, buf);

		count++;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_scfg.num_devices) /
						g_scfg.large_block_reads_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -(int64_t)g_scfg.max_lag_usec) {
			fprintf(stdout, "ERROR: large block reads can't keep up\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	free(buf);

	return NULL;
}

//------------------------------------------------
// Runs in every device large-block write thread,
// executes large-block writes at a constant rate.
//
static void*
run_large_block_writes(void* pv_dev)
{
	rand_seed_thread();

	device* dev = (device*)pv_dev;

	uint8_t* buf = act_valloc(g_scfg.large_block_ops_bytes);

	if (! buf) {
		fprintf(stdout, "ERROR: large block write buffer act_valloc()\n");
		g_running = false;
		return NULL;
	}

	uint64_t count = 0;

	while (g_running) {
		write_and_report_large_block(dev, buf, count);

		count++;

		uint64_t target_us = (uint64_t)
				((double)(count * 1000000 * g_scfg.num_devices) /
						g_scfg.large_block_writes_per_sec);

		int64_t sleep_us = (int64_t)(target_us - (get_us() - g_run_start_us));

		if (sleep_us > 0) {
			usleep((uint32_t)sleep_us);
		}
		else if (sleep_us < -(int64_t)g_scfg.max_lag_usec) {
			fprintf(stdout, "ERROR: large block writes can't keep up\n");
			fprintf(stdout, "drive(s) can't keep up - test stopped\n");
			g_running = false;
		}
	}

	free(buf);

	return NULL;
}

//------------------------------------------------
// Runs in every device tomb raider thread,
// executes continuous large-block reads.
//
static void*
run_tomb_raider(void* pv_dev)
{
	device* dev = (device*)pv_dev;

	uint8_t* buf = act_valloc(g_scfg.large_block_ops_bytes);

	if (! buf) {
		fprintf(stdout, "ERROR: tomb raider buffer act_valloc()\n");
		g_running = false;
		return NULL;
	}

	uint64_t offset = 0;
	uint64_t end = dev->n_large_blocks * g_scfg.large_block_ops_bytes;

	while (g_running) {
		if (g_scfg.tomb_raider_sleep_us != 0) {
			usleep(g_scfg.tomb_raider_sleep_us);
		}

		read_from_device(dev, offset, g_scfg.large_block_ops_bytes, buf);

		offset += g_scfg.large_block_ops_bytes;

		if (offset == end) {
			offset = 0;
		}
	}

	free(buf);

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
	rand_seed_thread();

	queue* req_q = (queue*)pv_req_q;
	trans_req req;

	while (g_running) {
		if (queue_pop(req_q, (void*)&req, 100) != QUEUE_OK) {
			continue;
		}

		uint8_t stack_buffer[req.size + 4096];
		uint8_t* buf = align_4096(stack_buffer);

		if (req.is_write) {
			write_and_report(&req, buf);
		}
		else {
			read_and_report(&req, buf);
		}

		atomic32_decr(&g_reqs_queued);
	}

	return NULL;
}


//==========================================================
// Local helpers - generic.
//

//------------------------------------------------
// Aligned memory allocation.
//
static uint8_t*
act_valloc(size_t size)
{
	void* pv;

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
}

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

	uint64_t device_bytes;

	if (g_scfg.file_size == 0) {
		ioctl(fd, BLKGETSIZE64, &device_bytes);
	}
	else { // undocumented file mode
		device_bytes = g_scfg.file_size;

		if (ftruncate(fd, (off_t)device_bytes) != 0) {
			fprintf(stdout, "ERROR: ftruncate file %s errno %d '%s'\n",
					dev->name, errno, act_strerror(errno));
			fd_put(dev, fd);
			return false;
		}
	}

	dev->n_large_blocks = device_bytes / g_scfg.large_block_ops_bytes;
	dev->min_op_bytes = discover_min_op_bytes(fd, dev->name);
	fd_put(dev, fd);

	if (dev->n_large_blocks == 0) {
		fprintf(stdout, "ERROR: %s ioctl to discover size\n", dev->name);
		return false;
	}

	if (dev->min_op_bytes == 0) {
		return false;
	}

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks, "
			"minimum IO size = %" PRIu32 " bytes\n",
			dev->name, device_bytes, dev->n_large_blocks,
			dev->min_op_bytes);

	discover_read_pattern(dev);

	if (g_scfg.commit_to_device) {
		discover_write_pattern(dev);
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
	uint8_t* buf = act_valloc(HI_IO_MIN_SIZE);

	if (! buf) {
		fprintf(stdout, "ERROR: IO min size buffer act_valloc()\n");
		return 0;
	}

	size_t read_sz = LO_IO_MIN_SIZE;

	while (read_sz <= HI_IO_MIN_SIZE) {
		if (pread_all(fd, (void*)buf, read_sz, 0)) {
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
discover_read_pattern(device* dev)
{
	// Total number of "min-op"-sized blocks on the device. (Excluding
	// fractional large block at end of device, if such.)
	uint64_t n_min_op_blocks =
			(dev->n_large_blocks * g_scfg.large_block_ops_bytes) /
					dev->min_op_bytes;

	// Number of "min-op"-sized blocks per (smallest) read request.
	uint32_t read_req_min_op_blocks =
			(g_scfg.record_stored_bytes + dev->min_op_bytes - 1) /
					dev->min_op_bytes;

	// Size in bytes per (smallest) read request.
	dev->read_bytes = read_req_min_op_blocks * dev->min_op_bytes;

	// Number of "min-op"-sized blocks per (largest) read request.
	uint32_t read_req_min_op_blocks_rmx =
			(g_scfg.record_stored_bytes_rmx + dev->min_op_bytes - 1) /
					dev->min_op_bytes;

	// Number of read request sizes in configured range.
	dev->n_read_sizes =
			read_req_min_op_blocks_rmx - read_req_min_op_blocks + 1;

	// Total number of sites on device to read from. (Make sure the last site
	// has room for largest possible read request.)
	dev->n_read_offsets = n_min_op_blocks - read_req_min_op_blocks_rmx + 1;
}

//------------------------------------------------
// Discover device's write request pattern.
//
static void
discover_write_pattern(device* dev)
{
	// Use the larger of min-op bytes and configured commit-min-bytes.
	dev->min_commit_bytes = dev->min_op_bytes > g_scfg.commit_min_bytes ?
			dev->min_op_bytes : g_scfg.commit_min_bytes;

	// Total number of "min-commit"-sized blocks on the device. (Excluding
	// fractional large block at end of device, if such.)
	uint64_t n_min_commit_blocks =
			(dev->n_large_blocks * g_scfg.large_block_ops_bytes) /
					dev->min_commit_bytes;

	// Number of "min-commit"-sized blocks per (smallest) write request.
	uint32_t write_req_min_commit_blocks =
			(g_scfg.record_stored_bytes + dev->min_commit_bytes - 1) /
					dev->min_commit_bytes;

	// Size in bytes per (smallest) write request.
	dev->write_bytes = write_req_min_commit_blocks * dev->min_commit_bytes;

	// Number of "min-commit"-sized blocks per (largest) write request.
	uint32_t write_req_min_commit_blocks_rmx =
			(g_scfg.record_stored_bytes_rmx + dev->min_commit_bytes - 1) /
					dev->min_commit_bytes;

	// Number of write request sizes in configured range.
	dev->n_write_sizes =
			write_req_min_commit_blocks_rmx - write_req_min_commit_blocks + 1;

	// Total number of sites on device to write to. (Make sure the last site
	// has room for largest possible write request.)
	dev->n_write_offsets =
			n_min_commit_blocks - write_req_min_commit_blocks_rmx + 1;
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
		int direct_flags = O_DIRECT | (g_scfg.disable_odsync ? 0 : O_DSYNC);
		int flags = O_RDWR | (g_scfg.file_size == 0 ? direct_flags : O_CREAT);

		fd = open(dev->name, flags, S_IRUSR | S_IWUSR);

		if (fd == -1) {
			fprintf(stdout, "ERROR: open device %s errno %d '%s'\n", dev->name,
					errno, act_strerror(errno));
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
	uint64_t stop_time = read_from_device(read_req->dev, read_req->offset,
			read_req->size, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_read_hist,
				safe_delta_ns(read_req->start_time, stop_time));
		histogram_insert_data_point(read_req->dev->raw_read_hist,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one large block read operation and report.
//
static void
read_and_report_large_block(device* dev, uint8_t* buf)
{
	uint64_t offset = random_large_block_offset(dev);
	uint64_t start_time = get_ns();
	uint64_t stop_time = read_from_device(dev, offset,
			g_scfg.large_block_ops_bytes, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_large_block_read_hist,
				safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device read operation.
//
static uint64_t
read_from_device(device* dev, uint64_t offset, uint32_t size, uint8_t* buf)
{
	int fd = fd_get(dev);

	if (fd == -1) {
		return -1;
	}

	if (! pread_all(fd, buf, size, offset)) {
		close(fd);
		fprintf(stdout, "ERROR: reading %s: %d '%s'\n", dev->name, errno,
				act_strerror(errno));
		return -1;
	}

	uint64_t stop_ns = get_ns();

	fd_put(dev, fd);

	return stop_ns;
}

//------------------------------------------------
// Do one transaction write operation and report.
//
static void
write_and_report(trans_req* write_req, uint8_t* buf)
{
	// Salt each record.
	rand_fill(buf, write_req->size);

	uint64_t raw_start_time = get_ns();
	uint64_t stop_time = write_to_device(write_req->dev, write_req->offset,
			write_req->size, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_raw_write_hist,
				safe_delta_ns(raw_start_time, stop_time));
		histogram_insert_data_point(g_write_hist,
				safe_delta_ns(write_req->start_time, stop_time));
		histogram_insert_data_point(write_req->dev->raw_write_hist,
				safe_delta_ns(raw_start_time, stop_time));
	}
}

//------------------------------------------------
// Do one large block write operation and report.
//
static void
write_and_report_large_block(device* dev, uint8_t* buf, uint64_t count)
{
	// Salt the block each time.
	rand_fill(buf, g_scfg.large_block_ops_bytes);

	uint64_t offset = random_large_block_offset(dev);
	uint64_t start_time = get_ns();
	uint64_t stop_time = write_to_device(dev, offset,
			g_scfg.large_block_ops_bytes, buf);

	if (stop_time != -1) {
		histogram_insert_data_point(g_large_block_write_hist,
				safe_delta_ns(start_time, stop_time));
	}
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t
write_to_device(device* dev, uint64_t offset, uint32_t size, const uint8_t* buf)
{
	int fd = fd_get(dev);

	if (fd == -1) {
		return -1;
	}

	if (! pwrite_all(fd, buf, size, offset)) {
		close(fd);
		fprintf(stdout, "ERROR: writing %s: %d '%s'\n", dev->name, errno,
				act_strerror(errno));
		return -1;
	}

	uint64_t stop_ns = get_ns();

	fd_put(dev, fd);

	return stop_ns;
}
