/*
 * cfg.c
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

#include "cfg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


//==========================================================
// Typedefs & constants.
//

static const char* const SCHEDULER_MODES[] = {
	"noop", // default
	"cfq"
};

static const uint32_t N_SCHEDULER_MODES =
		(uint32_t)(sizeof(SCHEDULER_MODES) / sizeof(const char*));


//==========================================================
// Public API.
//

void
parse_device_names(size_t max_num_devices, char names[][MAX_DEVICE_NAME_SIZE],
		uint32_t* p_num_devices)
{
	const char* val;

	while ((val = strtok(NULL, ",;" WHITE_SPACE)) != NULL) {
		if (*p_num_devices == max_num_devices) {
			printf("ERROR: too many device names\n");
			*p_num_devices = 0;
			return;
		}

		size_t name_len = strlen(val);

		if (name_len == 0 || name_len >= MAX_DEVICE_NAME_SIZE) {
			printf("ERROR: bad device name '%s'\n", val);
			*p_num_devices = 0;
			return;
		}

		strcpy(names[*p_num_devices], val);
		(*p_num_devices)++;
	}
}

const char*
parse_scheduler_mode()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	if (val == NULL) {
		printf("ERROR: missing scheduler mode - using 'noop'\n");
		return "noop";
	}

	for (uint32_t m = 0; m < N_SCHEDULER_MODES; m++) {
		if (strcmp(val, SCHEDULER_MODES[m]) == 0) {
			return SCHEDULER_MODES[m];
		}
	}

	printf("ERROR: unknown scheduler mode '%s' - using 'noop'\n", val);

	return "noop";
}

uint32_t
parse_uint32()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	if (val == NULL) {
		printf("ERROR: missing integer config value\n");
		return 0;
	}

	uint64_t u64_val = strtoul(val, NULL, 10);

	if (u64_val > UINT32_MAX) {
		printf("ERROR: %s overflows unsigned int\n", val);
		return 0;
	}

	return (uint32_t)u64_val;
}

bool
parse_yes_no()
{
	const char* val = strtok(NULL, WHITE_SPACE);

	return val != NULL && *val == 'y';
}
