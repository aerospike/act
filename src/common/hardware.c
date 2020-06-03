/*
 * hardware.c
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

#include "hardware.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trace.h"


//==========================================================
// Typedefs & constants.
//

typedef enum {
	FILE_RES_OK,
	FILE_RES_NOT_FOUND,
	FILE_RES_ERROR
} file_res;


//==========================================================
// Forward declarations.
//

static file_res read_list(const char* path, cpu_set_t* mask);
static file_res read_index(const char* path, uint16_t* val);
static file_res read_file(const char* path, void* buf, size_t* limit);


//==========================================================
// Public API.
//

uint32_t
num_cpus()
{
	cpu_set_t os_cpus_online;

	if (read_list("/sys/devices/system/cpu/online", &os_cpus_online) !=
			FILE_RES_OK) {
		printf("ERROR: couldn't read list of online CPUs\n");
		return 0;
	}

	uint16_t n_cpus = 0;
	uint16_t n_os_cpus;

	for (n_os_cpus = 0; n_os_cpus < CPU_SETSIZE; n_os_cpus++) {
		char path[1000];

		snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%hu/topology/physical_package_id",
				n_os_cpus);

		uint16_t i_os_package;
		file_res res = read_index(path, &i_os_package);

		if (res == FILE_RES_NOT_FOUND) {
			break; // we've processed all available CPUs - done
		}

		if (res != FILE_RES_OK) {
			printf("ERROR: reading OS package index from %s\n", path);
			return 0;
		}

		// Only consider CPUs that are actually in use.
		if (CPU_ISSET(n_os_cpus, &os_cpus_online)) {
			n_cpus++;
		}
	}

	if (n_os_cpus == CPU_SETSIZE) {
		printf("ERROR: too many CPUs\n");
		return 0;
	}

	printf("detected %" PRIu32 " CPUs\n\n", n_cpus);

	return n_cpus;
}

void
set_scheduler(const char* device_name, const char* mode)
{
	// TODO - could be much more general, like the latest Aerospike server, but
	// for now let's just keep it really simple.

	const char* last_slash = strrchr(device_name, '/');
	const char* device_tag = last_slash ? last_slash + 1 : device_name;

	char scheduler_file_name[128];

	strcpy(scheduler_file_name, "/sys/block/");
	strcat(scheduler_file_name, device_tag);
	strcat(scheduler_file_name, "/queue/scheduler");

	FILE* scheduler_file = fopen(scheduler_file_name, "w");

	if (scheduler_file == NULL) {
		printf("ERROR: couldn't open %s errno %d '%s'\n", scheduler_file_name,
				errno, act_strerror(errno));
		return;
	}

	if (fwrite(mode, strlen(mode), 1, scheduler_file) != 1) {
		printf("ERROR: writing %s to %s errno %d '%s'\n", mode,
				scheduler_file_name, errno, act_strerror(errno));
	}

	fclose(scheduler_file);
}


//==========================================================
// Local helpers.
//

static file_res
read_list(const char* path, cpu_set_t* mask)
{
	char buf[1000];
	size_t limit = sizeof(buf);
	file_res res = read_file(path, buf, &limit);

	if (res != FILE_RES_OK) {
		return res;
	}

	buf[limit - 1] = '\0';
	CPU_ZERO(mask);

	char* at = buf;

	while (true) {
		char* delim;
		uint64_t from = strtoul(at, &delim, 10);
		uint64_t thru;

		if (*delim == ',' || *delim == '\0'){
			thru = from;
		}
		else if (*delim == '-') {
			at = delim + 1;
			thru = strtoul(at, &delim, 10);
		}
		else {
			printf("ERROR: invalid list '%s' in %s\n", buf, path);
			return FILE_RES_ERROR;
		}

		if (from >= CPU_SETSIZE || thru >= CPU_SETSIZE || from > thru) {
			printf("ERROR: invalid list '%s' in %s\n", buf, path);
			return FILE_RES_ERROR;
		}

		for (size_t i = from; i <= thru; ++i) {
			CPU_SET(i, mask);
		}

		if (*delim == '\0') {
			break;
		}

		at = delim + 1;
	}

	return FILE_RES_OK;
}

static file_res
read_index(const char* path, uint16_t* val)
{
	char buf[100];
	size_t limit = sizeof(buf);
	file_res res = read_file(path, buf, &limit);

	if (res != FILE_RES_OK) {
		return res;
	}

	buf[limit - 1] = '\0';

	char* end;
	uint64_t x = strtoul(buf, &end, 10);

	if (*end != '\0' || x >= CPU_SETSIZE) {
		printf("ERROR: invalid index '%s' in %s\n", buf, path);
		return FILE_RES_ERROR;
	}

	*val = (uint16_t)x;

	return FILE_RES_OK;
}

static file_res
read_file(const char* path, void* buf, size_t* limit)
{
	int32_t fd = open(path, O_RDONLY);

	if (fd < 0) {
		if (errno == ENOENT) {
			return FILE_RES_NOT_FOUND;
		}

		printf("ERROR: couldn't open file %s for reading: %d '%s'\n", path,
				errno, act_strerror(errno));
		return FILE_RES_ERROR;
	}

	size_t total = 0;

	while (total < *limit) {
		ssize_t len = read(fd, (uint8_t*)buf + total, *limit - total);

		if (len < 0) {
			printf("ERROR: couldn't read file %s: %d '%s'\n", path, errno,
					act_strerror(errno));
			close(fd);
			return FILE_RES_ERROR;
		}

		if (len == 0) {
			break; // EOF
		}

		total += (size_t)len;
	}

	close(fd);

	if (total == *limit) {
		printf("ERROR: read buffer too small for file %s\n", path);
		return FILE_RES_ERROR;
	}

	*limit = total;

	return FILE_RES_OK;
}
