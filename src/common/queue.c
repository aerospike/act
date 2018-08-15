/*
 * queue.c
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

#include "queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


//==========================================================
// Typedefs & constants.
//

#define Q_ALLOC_SZ (64 * 1024)


//==========================================================
// Forward Declarations
//

int q_resize(queue* q, uint new_sz);
void q_unwrap(queue* q);


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
queue_create(size_t ele_size, bool thread_safe)
{
	queue* q = NULL;

	q = malloc( sizeof(queue));

	if (! q) {
		fprintf(stdout, "ERROR: creating queue (malloc)\n");
		return NULL;
	}

	q->elements = malloc(Q_ALLOC_SZ * ele_size);

	if (! q->elements) {
		fprintf(stdout, "ERROR: creating queue (malloc)\n");
		free(q);
		return NULL;
	}

	q->alloc_sz = Q_ALLOC_SZ;
	q->write_offset = q->read_offset = 0;
	q->ele_size = ele_size;
	q->thread_safe = thread_safe;

	if (! q->thread_safe) {
		return q;
	}

	if (pthread_mutex_init(&q->lock, NULL) != 0) {
		fprintf(stdout, "ERROR: creating queue (mutex init)\n");
		free(q->elements);
		free(q);
		return NULL;
	}

	if (pthread_cond_init(&q->cond_var, NULL) != 0) {
		fprintf(stdout, "ERROR: creating queue (cond init)\n");
		pthread_mutex_destroy(&q->lock);
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
	if (q->thread_safe) {
		pthread_cond_destroy(&q->cond_var);
		pthread_mutex_destroy(&q->lock);
	}

	free(q->elements);
	free(q);
}

//------------------------------------------------
// Get the number of elements in the queue.
//
uint32_t
queue_sz(queue* q)
{
	if (q->thread_safe) {
		pthread_mutex_lock(&q->lock);
	}

	uint32_t rv = Q_SZ(q);

	if (q->thread_safe) {
		pthread_mutex_unlock(&q->lock);
	}

	return rv;
}

//------------------------------------------------
// Push an element to the tail of the queue.
//
int
queue_push(queue* q, const void* ele_ptr)
{
	if (q->thread_safe) {
		pthread_mutex_lock(&q->lock);
	}

	if (Q_SZ(q) == q->alloc_sz) {
		if (q_resize(q, q->alloc_sz * 2) != QUEUE_OK) {
			if (q->thread_safe) {
				pthread_mutex_unlock(&q->lock);
			}

			return QUEUE_ERR;
		}
	}

	memcpy(Q_ELE_PTR(q, q->write_offset), ele_ptr, q->ele_size);
	q->write_offset++;

	// We're at risk of overflowing the write offset if it's too big.
	if (q->write_offset & 0xC0000000) {
		q_unwrap(q);
	}

	if (q->thread_safe) {
		pthread_cond_signal(&q->cond_var);
		pthread_mutex_unlock(&q->lock);
	}

	return QUEUE_OK;
}

//------------------------------------------------
// Pop an element from the head of the queue.
//
// ms_wait < 0 - wait forever
// ms_wait = 0 - don't wait at all
// ms_wait > 0 - wait that number of milliseconds
//
int
queue_pop(queue* q, void* ele_ptr, int ms_wait)
{
	if (q->thread_safe) {
		pthread_mutex_lock(&q->lock);
	}

	if (q->thread_safe) {
		struct timespec tp;

		if (ms_wait > 0) {
			clock_gettime(CLOCK_REALTIME, &tp);
			tp.tv_sec += ms_wait / 1000;
			tp.tv_nsec += (ms_wait % 1000) * 1000000;

			if (tp.tv_nsec > 1000000000) {
				tp.tv_nsec -= 1000000000;
				tp.tv_sec++;
			}
		}

		// Note that we apparently have to use a while loop. Careful reading of
		// the pthread_cond_signal() documentation says that AT LEAST ONE
		// waiting thread will be awakened...

		while (Q_EMPTY(q)) {
			if (ms_wait == QUEUE_FOREVER) {
				pthread_cond_wait(&q->cond_var, &q->lock);
			}
			else if (ms_wait == QUEUE_NO_WAIT) {
				pthread_mutex_unlock(&q->lock);
				return QUEUE_EMPTY;
			}
			else {
				pthread_cond_timedwait(&q->cond_var, &q->lock, &tp);

				if (Q_EMPTY(q)) {
					pthread_mutex_unlock(&q->lock);
					return QUEUE_EMPTY;
				}
			}
		}
	}
	else if (Q_EMPTY(q)) {
		return QUEUE_EMPTY;
	}

	memcpy(ele_ptr, Q_ELE_PTR(q, q->read_offset), q->ele_size);
	q->read_offset++;

	if (q->read_offset == q->write_offset) {
		q->read_offset = q->write_offset = 0;
	}

	if (q->thread_safe) {
		pthread_mutex_unlock(&q->lock);
	}

	return QUEUE_OK;
}


//==========================================================
// Local helpers.
//

//------------------------------------------------
// Change allocated capacity - called under lock.
//
int
q_resize(queue* q, uint new_sz)
{
	// The rare case where the queue is not fragmented, and none of the offsets
	// need to move.
	if (q->read_offset % q->alloc_sz == 0) {
		q->elements = realloc(q->elements, new_sz * q->ele_size);

		if (! q->elements) {
			fprintf(stdout, "ERROR: resizing queue (realloc)\n");
			return QUEUE_ERR;
		}

		q->read_offset = 0;
		q->write_offset = q->alloc_sz;
	}
	else {
		uint8_t* new_q = malloc(new_sz * q->ele_size);

		if (! new_q) {
			fprintf(stdout, "ERROR: resizing queue (malloc)\n");
			return QUEUE_ERR;
		}

		// endsz is used bytes in old queue from insert point to end.
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

	return QUEUE_OK;
}

//------------------------------------------------
// Reset read & write offsets - called under lock.
//
void
q_unwrap(queue* q)
{
	uint32_t sz = Q_SZ(q);

	q->read_offset %= q->alloc_sz;
	q->write_offset = q->read_offset + sz;
}
