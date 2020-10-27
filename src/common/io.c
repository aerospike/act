/*
 * io.c
 *
 * Copyright (c) 2020 Aerospike, Inc. All rights reserved.
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

#include "io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>


//==========================================================
// Public API.
//

bool
pread_all(int fd, void* buf, size_t size, off_t offset)
{
	ssize_t result;

	while ((result = pread(fd, buf, size, offset)) != (ssize_t)size) {
		if (result < 0) {
			return false; // let the caller log errors
		}

		if (result == 0) { // should only happen if caller passed 0 size
			printf("ERROR: pread() returned 0\n");
			return false;
		}

		buf += result;
		offset += result;
		size -= result;
	}

	return true;
}

bool
pwrite_all(int fd, const void* buf, size_t size, off_t offset)
{
	ssize_t result;

	while ((result = pwrite(fd, buf, size, offset)) != (ssize_t)size) {
		if (result < 0) {
			return false; // let the caller log errors
		}

		if (result == 0) { // should only happen if caller passed 0 size
			printf("ERROR: pwrite() returned 0\n");
			return false;
		}

		buf += result;
		offset += result;
		size -= result;
	}

	return true;
}

bool
write_all(int fd, const void* buf, size_t size)
{
	ssize_t result;

	while ((result = write(fd, buf, size)) != (ssize_t)size) {
		if (result < 0) {
			return false; // let the caller log errors
		}

		if (result == 0) { // should only happen if caller passed 0 size
			printf("ERROR: write() returned 0\n");
			return false;
		}

		buf += result;
		size -= result;
	}

	return true;
}
