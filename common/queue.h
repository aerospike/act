/*
 * queue.h
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

#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cf_queue_s {
	bool			threadsafe;
	uint32_t		allocsz;		// number of elements currently allocated
	uint32_t		write_offset;	// tail of queue
	uint32_t		read_offset;	// head of queue - write is always >= read
	size_t			elementsz;		// number of bytes in an element
	pthread_mutex_t	LOCK;			// the mutex lock
	pthread_cond_t	CV;				// the condvar
	uint8_t*		queue;			// the elements' bytes
} cf_queue;

extern cf_queue *cf_queue_create(size_t elementsz, bool threadsafe);
extern void cf_queue_destroy(cf_queue *q);
extern int cf_queue_sz(cf_queue *q);
extern int cf_queue_push(cf_queue *q, void *ptr);
extern int cf_queue_pop(cf_queue *q, void *buf, int mswait);

// Returned by cf_queue_push() and/or cf_queue_pop():
#define CF_QUEUE_EMPTY -2
#define CF_QUEUE_ERR -1
#define CF_QUEUE_OK 0

// Possible mswait values to pass to cf_queue_pop():
#define CF_QUEUE_FOREVER -1
#define CF_QUEUE_NOWAIT 0
