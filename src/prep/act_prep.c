/*
 * salt.c
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
// Includes
//

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "common/random.h"
#include "common/trace.h"


//==========================================================
// Constants
//

const uint32_t NUM_SALT_THREADS = 8;
const uint32_t NUM_ZERO_THREADS = 8;
const uint32_t LARGE_BLOCK_BYTES = 1024 * 128;

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif


//==========================================================
// Globals
//

static char* g_device_name = NULL;
static uint64_t g_num_large_blocks = 0;
static uint8_t* g_p_zero_buffer = NULL;
static uint64_t g_blocks_per_salt_thread = 0;
static uint64_t g_blocks_per_zero_thread = 0;
static uint64_t g_extra_blocks_to_zero = 0;
static uint64_t g_extra_blocks_to_salt = 0;
static uint64_t g_extra_bytes_to_zero = 0;


//==========================================================
// Forward Declarations
//

static void*			run_salt(void* pv_n);
static void*			run_zero(void* pv_n);

static inline uint8_t*	cf_valloc(size_t size);
static bool				create_zero_buffer();
static bool				discover_num_blocks();
static inline int		fd_get();
static void				set_scheduler();


//==========================================================
// Main
//

int main(int argc, char* argv[]) {
	signal_setup();

	if (argc != 2) {
		fprintf(stdout, "usage: salt [device name]\n");
		exit(0);
	}

	char device_name[strlen(argv[1]) + 1];

	strcpy(device_name, argv[1]);
	g_device_name = device_name;

	set_scheduler();

	if (! discover_num_blocks()) {
		exit(-1);
	}

	//------------------------
	// Begin zeroing.

	fprintf(stdout, "cleaning device %s\n", g_device_name);

	if (! create_zero_buffer()) {
		exit(-1);
	}

	pthread_t zero_threads[NUM_ZERO_THREADS];

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		if (pthread_create(&zero_threads[n], NULL, run_zero,
				(void*)(uint64_t)n)) {
			fprintf(stdout, "ERROR: creating zero thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		void* pv_value;

		pthread_join(zero_threads[n], &pv_value);
	}

	free(g_p_zero_buffer);

	//------------------------
	// Begin salting.

	fprintf(stdout, "salting device %s\n", g_device_name);

	srand(time(NULL));

	if (! rand_seed()) {
		exit(-1);
	}

	pthread_t salt_threads[NUM_SALT_THREADS];

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		if (pthread_create(&salt_threads[n], NULL, run_salt,
				(void*)(uint64_t)n)) {
			fprintf(stdout, "ERROR: creating salt thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		void* pv_value;

		pthread_join(salt_threads[n], &pv_value);
	}

	return 0;
}


//==========================================================
// Thread "Run" Functions
//

//------------------------------------------------
// Runs in all (NUM_SALT_THREADS) salt_threads,
// salts a portion of the device.
//
static void* run_salt(void* pv_n) {
	uint32_t n = (uint32_t)(uint64_t)pv_n;

	uint64_t offset = n * g_blocks_per_salt_thread * LARGE_BLOCK_BYTES;
	uint64_t blocks_to_salt = g_blocks_per_salt_thread;
	uint64_t progress_blocks = 0;
	bool last_thread = n + 1 == NUM_SALT_THREADS;

	if (last_thread) {
		blocks_to_salt += g_extra_blocks_to_salt;
		progress_blocks = blocks_to_salt / 100;

		if (! progress_blocks) {
			progress_blocks = 1;
		}
	}

//	fprintf(stdout, "thread %d: blks-to-salt = %" PRIu64 ", prg-blks = %"
//		PRIu64 "\n", n, blocks_to_salt, progress_blocks);

	uint8_t* p_buffer = cf_valloc(LARGE_BLOCK_BYTES);

	if (! p_buffer) {
		fprintf(stdout, "ERROR: valloc in salt thread %" PRIu32 "\n", n);
		return NULL;
	}

	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: open in salt thread %" PRIu32 "\n", n);
		free(p_buffer);
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		fprintf(stdout, "ERROR: seek in salt thread %" PRIu32 "\n", n);
		close(fd);
		free(p_buffer);
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_salt; b++) {
		if (! rand_fill(p_buffer, LARGE_BLOCK_BYTES)) {
			fprintf(stdout, "ERROR: rand fill in salt thread %" PRIu32 "\n", n);
			break;
		}

		if (write(fd, p_buffer, LARGE_BLOCK_BYTES) !=
				(ssize_t)LARGE_BLOCK_BYTES) {
			fprintf(stdout, "ERROR: write in salt thread %" PRIu32 "\n", n);
			break;
		}

		if (progress_blocks && ! (b % progress_blocks)) {
			fprintf(stdout, ".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		fprintf(stdout, "\n");
	}

	close(fd);
	free(p_buffer);

	return NULL;
}

//------------------------------------------------
// Runs in all (NUM_ZERO_THREADS) zero_threads,
// zeros a portion of the device.
//
static void* run_zero(void* pv_n) {
	uint32_t n = (uint32_t)(uint64_t)pv_n;

	uint64_t offset = n * g_blocks_per_zero_thread * LARGE_BLOCK_BYTES;
	uint64_t blocks_to_zero = g_blocks_per_zero_thread;
	uint64_t progress_blocks = 0;
	bool last_thread = n + 1 == NUM_ZERO_THREADS;

	if (last_thread) {
		blocks_to_zero += g_extra_blocks_to_zero;
		progress_blocks = blocks_to_zero / 100;

		if (! progress_blocks) {
			progress_blocks = 1;
		}
	}

//	fprintf(stdout, "thread %d: blks-to-zero = %" PRIu64 ", prg-blks = %"
//		PRIu64 "\n", n, blocks_to_zero, progress_blocks);

	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: open in zero thread %" PRIu32 "\n", n);
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		fprintf(stdout, "ERROR: seek in zero thread %" PRIu32 "\n", n);
		close(fd);
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_zero; b++) {
		if (write(fd, g_p_zero_buffer, LARGE_BLOCK_BYTES) !=
				(ssize_t)LARGE_BLOCK_BYTES) {
			fprintf(stdout, "ERROR: write in zero thread %" PRIu32 "\n", n);
			break;
		}

		if (progress_blocks && ! (b % progress_blocks)) {
			fprintf(stdout, ".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		fprintf(stdout, "\n");
	}

	if (last_thread) {
		if (write(fd, g_p_zero_buffer, g_extra_bytes_to_zero) !=
				(ssize_t)g_extra_bytes_to_zero) {
			fprintf(stdout, "ERROR: write in zero thread %" PRIu32 "\n", n);
		}
	}

	close(fd);

	return NULL;
}


//==========================================================
// Helpers
//

//------------------------------------------------
// Aligned memory allocation.
//
static inline uint8_t* cf_valloc(size_t size) {
	void* pv;

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
}

//------------------------------------------------
// Allocate and zero one large block sized buffer.
//
static bool create_zero_buffer() {
	g_p_zero_buffer = cf_valloc(LARGE_BLOCK_BYTES);

	if (! g_p_zero_buffer) {
		fprintf(stdout, "ERROR: zero buffer cf_valloc()\n");
		return false;
	}

	memset(g_p_zero_buffer, 0, LARGE_BLOCK_BYTES);

	return true;
}

//------------------------------------------------
// Discover device storage capacity.
//
static bool discover_num_blocks() {
	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: opening device %s\n", g_device_name);
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	close(fd);

	g_num_large_blocks = device_bytes / LARGE_BLOCK_BYTES;
	g_extra_bytes_to_zero = device_bytes % LARGE_BLOCK_BYTES;

	g_blocks_per_zero_thread = g_num_large_blocks / NUM_ZERO_THREADS;
	g_blocks_per_salt_thread = g_num_large_blocks / NUM_SALT_THREADS;

	g_extra_blocks_to_zero = g_num_large_blocks % NUM_ZERO_THREADS;
	g_extra_blocks_to_salt = g_num_large_blocks % NUM_SALT_THREADS;

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks\n",
		g_device_name, device_bytes, g_num_large_blocks);

	return true;
}

//------------------------------------------------
// Get a file descriptor.
//
static inline int fd_get() {
	return open(g_device_name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);
}

//------------------------------------------------
// Set device's system block scheduler to noop.
//
static void set_scheduler() {
	const char* p_slash = strrchr(g_device_name, '/');
	const char* device_tag = p_slash ? p_slash + 1 : g_device_name;

	char scheduler_file_name[128];

	strcpy(scheduler_file_name, "/sys/block/");
	strcat(scheduler_file_name, device_tag);
	strcat(scheduler_file_name, "/queue/scheduler");

	FILE* scheduler_file = fopen(scheduler_file_name, "w");

	if (! scheduler_file) {
		fprintf(stdout, "ERROR: couldn't open %s\n", scheduler_file_name);
		return;
	}

	if (fwrite("noop", 4, 1, scheduler_file) != 1) {
		fprintf(stdout, "ERROR: writing noop to %s\n", scheduler_file_name);
	}

	fclose(scheduler_file);
}
