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
// Includes
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
// Constants & Macros
//

#define CF_QUEUE_ALLOCSZ (64 * 1024)

#define CF_Q_SZ(__q) (__q->write_offset - __q->read_offset)
#define CF_Q_EMPTY(__q) (__q->write_offset == __q->read_offset)
#define CF_Q_ELEM_PTR(__q, __i) (&q->queue[(__i % __q->allocsz) * q->elementsz])


//==========================================================
// Forward Declarations
//

int cf_queue_resize(cf_queue *q, uint new_sz);
void cf_queue_unwrap(cf_queue *q);


//==========================================================
// Public API
//

//------------------------------------------------
// Create a queue.
//
cf_queue*
cf_queue_create(size_t elementsz, bool threadsafe)
{
	cf_queue *q = NULL;

	q = malloc( sizeof(cf_queue));

	if (! q) {
		fprintf(stdout, "ERROR: creating queue (malloc)\n");
		return NULL;
	}

	q->queue = malloc(CF_QUEUE_ALLOCSZ * elementsz);

	if (! q->queue) {
		fprintf(stdout, "ERROR: creating queue (malloc)\n");
		free(q);
		return NULL;
	}

	q->allocsz = CF_QUEUE_ALLOCSZ;
	q->write_offset = q->read_offset = 0;
	q->elementsz = elementsz;
	q->threadsafe = threadsafe;

	if (! q->threadsafe) {
		return q;
	}

	if (0 != pthread_mutex_init(&q->LOCK, NULL)) {
		fprintf(stdout, "ERROR: creating queue (mutex init)\n");
		free(q->queue);
		free(q);
		return NULL;
	}

	if (0 != pthread_cond_init(&q->CV, NULL)) {
		fprintf(stdout, "ERROR: creating queue (cond init)\n");
		pthread_mutex_destroy(&q->LOCK);
		free(q->queue);
		free(q);
		return NULL;
	}

	return q;
}

//------------------------------------------------
// Destroy a queue.
//
void
cf_queue_destroy(cf_queue *q)
{
	if (q->threadsafe) {
		pthread_cond_destroy(&q->CV);
		pthread_mutex_destroy(&q->LOCK);
	}

	free(q->queue);
	free(q);
}

//------------------------------------------------
// Get the number of elements in the queue.
//
int
cf_queue_sz(cf_queue *q)
{
	int rv;

	if (q->threadsafe) {
		pthread_mutex_lock(&q->LOCK);
	}

	rv = CF_Q_SZ(q);

	if (q->threadsafe) {
		pthread_mutex_unlock(&q->LOCK);
	}

	return rv;
}

//------------------------------------------------
// Push an element to the tail of the queue.
//
int
cf_queue_push(cf_queue *q, void *ptr)
{
	if (q->threadsafe) {
		pthread_mutex_lock(&q->LOCK);
	}

	if (CF_Q_SZ(q) == q->allocsz) {
		if (CF_QUEUE_OK != cf_queue_resize(q, q->allocsz * 2)) {
			if (q->threadsafe) {
				pthread_mutex_unlock(&q->LOCK);
			}

			return CF_QUEUE_ERR;
		}
	}

	memcpy(CF_Q_ELEM_PTR(q, q->write_offset), ptr, q->elementsz);
	q->write_offset++;

	// We're at risk of overflowing the write offset if it's too big.
	if (q->write_offset & 0xC0000000) {
		cf_queue_unwrap(q);
	}

	if (q->threadsafe) {
		pthread_cond_signal(&q->CV);
		pthread_mutex_unlock(&q->LOCK);
	}

	return CF_QUEUE_OK;
}

//------------------------------------------------
// Pop an element from the head of the queue.
//
// ms_wait < 0 - wait forever
// ms_wait = 0 - don't wait at all
// ms_wait > 0 - wait that number of milliseconds
//
int
cf_queue_pop(cf_queue *q, void *buf, int ms_wait)
{
	if (q->threadsafe) {
		pthread_mutex_lock(&q->LOCK);
	}

	if (q->threadsafe) {
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

		while (CF_Q_EMPTY(q)) {
			if (CF_QUEUE_FOREVER == ms_wait) {
				pthread_cond_wait(&q->CV, &q->LOCK);
			}
			else if (CF_QUEUE_NOWAIT == ms_wait) {
				pthread_mutex_unlock(&q->LOCK);
				return CF_QUEUE_EMPTY;
			}
			else {
				pthread_cond_timedwait(&q->CV, &q->LOCK, &tp);

				if (CF_Q_EMPTY(q)) {
					pthread_mutex_unlock(&q->LOCK);
					return CF_QUEUE_EMPTY;
				}
			}
		}
	}
	else if (CF_Q_EMPTY(q)) {
		return CF_QUEUE_EMPTY;
	}

	memcpy(buf, CF_Q_ELEM_PTR(q, q->read_offset), q->elementsz);
	q->read_offset++;

	if (q->read_offset == q->write_offset) {
		q->read_offset = q->write_offset = 0;
	}

	if (q->threadsafe) {
		pthread_mutex_unlock(&q->LOCK);
	}

	return CF_QUEUE_OK;
}


//==========================================================
// Utilities
//

//------------------------------------------------
// Change allocated capacity - called under lock.
//
int
cf_queue_resize(cf_queue *q, uint new_sz)
{
	// The rare case where the queue is not fragmented, and none of the offsets
	// need to move.
	if (0 == q->read_offset % q->allocsz) {
		q->queue = realloc(q->queue, new_sz * q->elementsz);

		if (! q->queue) {
			fprintf(stdout, "ERROR: resizing queue (realloc)\n");
			return CF_QUEUE_ERR;
		}

		q->read_offset = 0;
		q->write_offset = q->allocsz;
	}
	else {
		uint8_t *newq = malloc(new_sz * q->elementsz);

		if (! newq) {
			fprintf(stdout, "ERROR: resizing queue (malloc)\n");
			return CF_QUEUE_ERR;
		}

		// endsz is used bytes in old queue from insert point to end.
		uint32_t endsz =
				(q->allocsz - (q->read_offset % q->allocsz)) * q->elementsz;

		memcpy(&newq[0], CF_Q_ELEM_PTR(q, q->read_offset), endsz);
		memcpy(&newq[endsz], &q->queue[0], (q->allocsz * q->elementsz) - endsz);

		free(q->queue);
		q->queue = newq;

		q->write_offset = q->allocsz;
		q->read_offset = 0;
	}

	q->allocsz = new_sz;

	return CF_QUEUE_OK;
}

//------------------------------------------------
// Reset read & write offsets - called under lock.
//
void
cf_queue_unwrap(cf_queue *q)
{
	int sz = CF_Q_SZ(q);

	q->read_offset %= q->allocsz;
	q->write_offset = q->read_offset + sz;
}
