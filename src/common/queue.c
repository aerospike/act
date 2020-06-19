/*
 * queue.c
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

#include "queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//==========================================================
// Typedefs & constants.
//

#define Q_ALLOC_SZ (64 * 1024)


//==========================================================
// Forward Declarations
//

static bool q_resize(queue* q, uint32_t new_sz);
static void q_unwrap(queue* q);


//==========================================================
// Inlines & macros.
//

#define Q_SZ(_q) (_q->write_offset - _q->read_offset)
#define Q_EMPTY(_q) (_q->write_offset == _q->read_offset)
#define Q_ELE_PTR(_q, _i) (&_q->elements[(_i % _q->alloc_sz) * _q->ele_size])


//==========================================================
// Public API.
//

//------------------------------------------------
// Create a queue.
//
queue*
queue_create(size_t ele_size)
{
	queue* q = malloc( sizeof(queue));

	if (q == NULL) {
		printf("ERROR: creating queue (malloc)\n");
		return NULL;
	}

	q->elements = malloc(Q_ALLOC_SZ * ele_size);

	if (q->elements == NULL) {
		printf("ERROR: creating queue (malloc)\n");
		free(q);
		return NULL;
	}

	q->alloc_sz = Q_ALLOC_SZ;
	q->write_offset = q->read_offset = 0;
	q->ele_size = ele_size;

	if (pthread_mutex_init(&q->lock, NULL) != 0) {
		printf("ERROR: creating queue (mutex init)\n");
		free(q->elements);
		free(q);
		return NULL;
	}

	return q;
}

//------------------------------------------------
// Destroy a queue.
//
void
queue_destroy(queue* q)
{
	pthread_mutex_destroy(&q->lock);

	free(q->elements);
	free(q);
}

//------------------------------------------------
// Get the number of elements in the queue.
//
uint32_t
queue_sz(queue* q)
{
	pthread_mutex_lock(&q->lock);

	uint32_t rv = Q_SZ(q);

	pthread_mutex_unlock(&q->lock);

	return rv;
}

//------------------------------------------------
// Push an element to the tail of the queue.
//
bool
queue_push(queue* q, const void* ele_ptr)
{
	pthread_mutex_lock(&q->lock);

	if (Q_SZ(q) == q->alloc_sz) {
		if (! q_resize(q, q->alloc_sz * 2)) {
			pthread_mutex_unlock(&q->lock);
			return false;
		}
	}

	memcpy(Q_ELE_PTR(q, q->write_offset), ele_ptr, q->ele_size);
	q->write_offset++;

	// We're at risk of overflowing the write offset if it's too big.
	if ((q->write_offset & 0xC0000000) != 0) {
		q_unwrap(q);
	}

	pthread_mutex_unlock(&q->lock);

	return true;
}

//------------------------------------------------
// Pop an element from the head of the queue.
//
bool
queue_pop(queue* q, void* ele_ptr)
{
	pthread_mutex_lock(&q->lock);

	if (Q_EMPTY(q)) {
		pthread_mutex_unlock(&q->lock);
		return false;
	}

	memcpy(ele_ptr, Q_ELE_PTR(q, q->read_offset), q->ele_size);
	q->read_offset++;

	if (q->read_offset == q->write_offset) {
		q->read_offset = q->write_offset = 0;
	}

	pthread_mutex_unlock(&q->lock);

	return true;
}


//==========================================================
// Local helpers.
//

//------------------------------------------------
// Change allocated capacity - called under lock.
//
static bool
q_resize(queue* q, uint32_t new_sz)
{
	if (q->read_offset % q->alloc_sz == 0) {
		// Queue not fragmented - just realloc.
		q->elements = realloc(q->elements, new_sz * q->ele_size);

		if (q->elements == NULL) {
			printf("ERROR: resizing queue (realloc)\n");
			return false;
		}

		q->read_offset = 0;
		q->write_offset = q->alloc_sz;
	}
	else {
		uint8_t* new_q = malloc(new_sz * q->ele_size);

		if (new_q == NULL) {
			printf("ERROR: resizing queue (malloc)\n");
			return false;
		}

		// end_size is used bytes in old queue from insert point to end.
		uint32_t end_size =
				(q->alloc_sz - (q->read_offset % q->alloc_sz)) * q->ele_size;

		memcpy(&new_q[0], Q_ELE_PTR(q, q->read_offset), end_size);
		memcpy(&new_q[end_size], &q->elements[0],
				(q->alloc_sz * q->ele_size) - end_size);

		free(q->elements);
		q->elements = new_q;

		q->write_offset = q->alloc_sz;
		q->read_offset = 0;
	}

	q->alloc_sz = new_sz;

	return true;
}

//------------------------------------------------
// Reset read & write offsets - called under lock.
//
static void
q_unwrap(queue* q)
{
	uint32_t sz = Q_SZ(q);

	q->read_offset %= q->alloc_sz;
	q->write_offset = q->read_offset + sz;
}
