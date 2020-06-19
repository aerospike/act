/*
 * act_prep.c
 *
 * Copyright (c) 2011-2020 Aerospike, Inc. All rights reserved.
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

#include "common/hardware.h"
#include "common/io.h"
#include "common/random.h"
#include "common/trace.h"


//==========================================================
// Typedefs & constants.
//

#define NUM_SALT_THREADS 8
#define NUM_ZERO_THREADS 8
#define LARGE_BLOCK_BYTES (1024 * 128)


//==========================================================
// Forward declarations.
//

static void* run_salt(void* pv_n);
static void* run_zero(void* pv_n);

static uint8_t* act_valloc(size_t size);
static bool create_zero_buffer();
static bool discover_num_blocks();


//==========================================================
// Globals.
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
// Inlines & macros.
//

static inline int
fd_get()
{
	// Note - not bothering to set O_DSYNC. Rigor is unnecessary for salting,
	// and we're not trying to measure performance here - just go fast.
	return open(g_device_name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);
}


//==========================================================
// Main.
//

int
main(int argc, char* argv[])
{
	signal_setup();

	if (argc != 2) {
		printf("usage: act_prep [device name]\n");
		exit(0);
	}

	char device_name[strlen(argv[1]) + 1];

	strcpy(device_name, argv[1]);
	g_device_name = device_name;

	set_scheduler(g_device_name, "noop");

	if (! discover_num_blocks()) {
		exit(-1);
	}

	//------------------------
	// Begin zeroing.

	printf("cleaning device %s\n", g_device_name);

	if (! create_zero_buffer()) {
		exit(-1);
	}

	pthread_t zero_threads[NUM_ZERO_THREADS];

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		if (pthread_create(&zero_threads[n], NULL, run_zero,
				(void*)(uint64_t)n) != 0) {
			printf("ERROR: creating zero thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		pthread_join(zero_threads[n], NULL);
	}

	free(g_p_zero_buffer);

	//------------------------
	// Begin salting.

	printf("salting device %s\n", g_device_name);

	rand_seed();

	pthread_t salt_threads[NUM_SALT_THREADS];

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		if (pthread_create(&salt_threads[n], NULL, run_salt,
				(void*)(uint64_t)n) != 0) {
			printf("ERROR: creating salt thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		pthread_join(salt_threads[n], NULL);
	}

	return 0;
}


//==========================================================
// Local helpers - thread "run" functions.
//

//------------------------------------------------
// Runs in all (NUM_SALT_THREADS) salt_threads,
// salts a portion of the device.
//
static void*
run_salt(void* pv_n)
{
	rand_seed_thread();

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

//	printf("thread %d: blks-to-salt = %" PRIu64 ", prg-blks = %" PRIu64 "\n", n,
//			blocks_to_salt, progress_blocks);

	uint8_t* buf = act_valloc(LARGE_BLOCK_BYTES);

	if (! buf) {
		printf("ERROR: valloc in salt thread %" PRIu32 "\n", n);
		return NULL;
	}

	int fd = fd_get();

	if (fd == -1) {
		printf("ERROR: open in salt thread %" PRIu32 "\n", n);
		free(buf);
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		printf("ERROR: seek in salt thread %" PRIu32 "\n", n);
		close(fd);
		free(buf);
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_salt; b++) {
		rand_fill(buf, LARGE_BLOCK_BYTES, 100);

		if (! write_all(fd, buf, LARGE_BLOCK_BYTES)) {
			printf("ERROR: write in salt thread %" PRIu32 "\n", n);
			break;
		}

		if (progress_blocks && ! (b % progress_blocks)) {
			printf(".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		printf("\n");
	}

	close(fd);
	free(buf);

	return NULL;
}

//------------------------------------------------
// Runs in all (NUM_ZERO_THREADS) zero_threads,
// zeros a portion of the device.
//
static void*
run_zero(void* pv_n)
{
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

//	printf("thread %d: blks-to-zero = %" PRIu64 ", prg-blks = %" PRIu64 "\n", n,
//			blocks_to_zero, progress_blocks);

	int fd = fd_get();

	if (fd == -1) {
		printf("ERROR: open in zero thread %" PRIu32 "\n", n);
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		printf("ERROR: seek in zero thread %" PRIu32 "\n", n);
		close(fd);
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_zero; b++) {
		if (! write_all(fd, g_p_zero_buffer, LARGE_BLOCK_BYTES)) {
			printf("ERROR: write in zero thread %" PRIu32 "\n", n);
			break;
		}

		if (progress_blocks && ! (b % progress_blocks)) {
			printf(".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		printf("\n");
	}

	if (last_thread) {
		if (! write_all(fd, g_p_zero_buffer, g_extra_bytes_to_zero)) {
			printf("ERROR: write in zero thread %" PRIu32 "\n", n);
		}
	}

	close(fd);

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

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : NULL;
}

//------------------------------------------------
// Allocate and zero one large block sized buffer.
//
static bool
create_zero_buffer()
{
	g_p_zero_buffer = act_valloc(LARGE_BLOCK_BYTES);

	if (! g_p_zero_buffer) {
		printf("ERROR: zero buffer act_valloc()\n");
		return false;
	}

	memset(g_p_zero_buffer, 0, LARGE_BLOCK_BYTES);

	return true;
}

//------------------------------------------------
// Discover device storage capacity.
//
static bool
discover_num_blocks()
{
	int fd = fd_get();

	if (fd == -1) {
		printf("ERROR: opening device %s\n", g_device_name);
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

	printf("%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks\n",
			g_device_name, device_bytes, g_num_large_blocks);

	return true;
}
