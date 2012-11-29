/*
 *  Aerospike Foundation
 *  src/queue.c - queue framework
 *
 * Copyright (c) 2008-2012 Aerospike, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "queue.h"

// #define DEBUG 1

/* SYNOPSIS
 * Queue
 */


/* cf_queue_create
 * Initialize a queue */
cf_queue *
cf_queue_create(size_t elementsz, bool threadsafe)
{
	cf_queue *q = NULL;

	q = malloc( sizeof(cf_queue));
	/* FIXME error msg */
	if (!q)
		return(NULL);
	q->allocsz = CF_QUEUE_ALLOCSZ;
	q->write_offset = q->read_offset = 0;
	q->elementsz = elementsz;
	q->threadsafe = threadsafe;

	q->queue = malloc(CF_QUEUE_ALLOCSZ * elementsz);
	if (! q->queue) {
		free(q);
		return(NULL);
	}

	if (!q->threadsafe)
		return(q);

	if (0 != pthread_mutex_init(&q->LOCK, NULL)) {
		/* FIXME error msg */
		free(q->queue);
		free(q);
		return(NULL);
	}

	if (0 != pthread_cond_init(&q->CV, NULL)) {
		pthread_mutex_destroy(&q->LOCK);
		free(q->queue);
		free(q);
		return(NULL);
	}

	return(q);
}

// TODO: probably need to wait until whoever is inserting or removing is finished
// Sort of. Anyone in a race with the destructor, who has a pointer to the queue,
// is in jepardy anyway.

void
cf_queue_destroy(cf_queue *q)
{
	if (q->threadsafe) {
		pthread_cond_destroy(&q->CV);
		pthread_mutex_destroy(&q->LOCK);
	}
	memset(q->queue, 0, sizeof(q->allocsz * q->elementsz));
	free(q->queue);
	memset(q, 0, sizeof(cf_queue) );
	free(q);
}

int
cf_queue_sz(cf_queue *q)
{
	int rv;

	if (q->threadsafe)
		pthread_mutex_lock(&q->LOCK);
	rv = CF_Q_SZ(q);
	if (q->threadsafe)
		pthread_mutex_unlock(&q->LOCK);
	return(rv);
	
}

//
// Internal function. Call with new size with lock held.
// *** THIS ONLY WORKS ON FULL QUEUES ***
//
int
cf_queue_resize(cf_queue *q, uint new_sz)
{
	// check - a lot of the code explodes badly if queue is not full
	if (CF_Q_SZ(q) != q->allocsz) {
		// cf_info(CF_QUEUE,"cf_queue: internal error: resize on non-full queue");
		return(-1);
	}
	
	// the rare case where the queue is not fragmented, and realloc makes sense
	// and none of the offsets need to move
	if (0 == q->read_offset % q->allocsz) {
		q->queue = realloc(q->queue, new_sz * q->elementsz);
		if (!q->queue) {
			// cf_info(CF_QUEUE," queue memory failure");
			return(-1);
		}
		q->read_offset = 0;
		q->write_offset = q->allocsz;
	}
	else {
		
		uint8_t *newq = malloc(new_sz * q->elementsz);
		if (!newq) {
			// cf_info(CF_QUEUE," queue resize memory failure");
			return(-1);
		}
		// endsz is used bytes in the old queue from the insert point to the end
		uint endsz = (q->allocsz - (q->read_offset % q->allocsz)) * q->elementsz;
		memcpy(&newq[0], CF_Q_ELEM_PTR(q, q->read_offset), endsz);
		memcpy(&newq[endsz], &q->queue[0], (q->allocsz * q->elementsz) - endsz); 
		
		free(q->queue);
		q->queue = newq;

		q->write_offset = q->allocsz;
		q->read_offset = 0;
	}

	q->allocsz = new_sz;
	return(0);	
}

// we have to guard against wraparound, call this occasionally
// I really expect this will never get called....
// HOWEVER it can be a symptom of a queue getting really, really deep
//
void
cf_queue_unwrap(cf_queue *q)
{
	// cf_debug(CF_QUEUE, " queue memory unwrap!!!! queue: %p",q);
	int sz = CF_Q_SZ(q);
	q->read_offset %= q->allocsz;
	q->write_offset = q->read_offset + sz;
}


/* cf_queue_push
 * 
 * */
int
cf_queue_push(cf_queue *q, void *ptr)
{
	/* FIXME arg check - and how do you do that, boyo? Magic numbers? */

	/* FIXME error */
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
			return(-1);

	/* Check queue length */
	if (CF_Q_SZ(q) == q->allocsz) {
		/* resize is a pain for circular buffers */
		if (0 != cf_queue_resize(q, q->allocsz * 2)) {
			if (q->threadsafe)
				pthread_mutex_unlock(&q->LOCK);
			// cf_warning(CF_QUEUE, "queue resize failure");
			return(-1);
		}
	}

	// todo: if queues are power of 2, this can be a shift
	memcpy(CF_Q_ELEM_PTR(q,q->write_offset), ptr, q->elementsz);
	q->write_offset++;
	// we're at risk of overflow if the write offset is that high
	if (q->write_offset & 0xC0000000) cf_queue_unwrap(q);
	
	if (q->threadsafe)
		pthread_cond_signal(&q->CV);

	/* FIXME blow a gasket */
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK)))
		return(-1);

	return(0);
}

/* cf_queue_push_head
 * Push head goes to the front, which currently means memcpying the entire queue contents
 * which is not the most optimal, but somewhat performant in most cases
 * */
int
cf_queue_push_head(cf_queue *q, void *ptr)
{

	/* FIXME error */
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
			return(-1);

	/* Check queue length */
	if (CF_Q_SZ(q) == q->allocsz) {
		/* resize is a pain for circular buffers */
		if (0 != cf_queue_resize(q, q->allocsz * 2)) {
			if (q->threadsafe)
				pthread_mutex_unlock(&q->LOCK);
			//cf_warning(CF_QUEUE, "queue resize failure");
			return(-1);
		}
	}
	
	// easy case, tail insert is head insert
	if (q->read_offset == q->write_offset) {
		memcpy(CF_Q_ELEM_PTR(q,q->write_offset), ptr, q->elementsz);
		q->write_offset++;
	}		
	// another easy case, there's space up front
	else if (q->read_offset > 0) {
		q->read_offset--;
		memcpy(CF_Q_ELEM_PTR(q,q->read_offset), ptr, q->elementsz);
	}
	// hard case, we're going to have to shift everything back
	else {
		memmove(CF_Q_ELEM_PTR(q, 1),CF_Q_ELEM_PTR(q, 0),q->elementsz * CF_Q_SZ(q) );
		memcpy(CF_Q_ELEM_PTR(q,0), ptr, q->elementsz);		
		q->write_offset++;
	}	
		
	// we're at risk of overflow if the write offset is that high
	if (q->write_offset & 0xC0000000) cf_queue_unwrap(q);
	
	if (q->threadsafe)
		pthread_cond_signal(&q->CV);

	/* FIXME blow a gasket */
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK)))
		return(-1);

	return(0);
}


/* cf_queue_pop
 * if ms_wait < 0, wait forever
 * if ms_wait = 0, don't wait at all
 * if ms_wait > 0, wait that number of ms
 * */
int
cf_queue_pop(cf_queue *q, void *buf, int ms_wait)
{
	if (NULL == q)
		return(-1);

	/* FIXME error checking */
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
		return(-1);

	struct timespec tp;
	if (ms_wait > 0) {
		clock_gettime( CLOCK_REALTIME, &tp); 
		tp.tv_sec += ms_wait / 1000;
		tp.tv_nsec += (ms_wait % 1000) * 1000000;
		if (tp.tv_nsec > 1000000000) {
			tp.tv_nsec -= 1000000000;
			tp.tv_sec++;
		}
	}

	/* FIXME error checking */
	/* Note that we apparently have to use a while() loop.  Careful reading
	 * of the pthread_cond_signal() documentation says that AT LEAST ONE
	 * waiting thread will be awakened... */
	if (q->threadsafe) {
		while (CF_Q_EMPTY(q)) {
			if (CF_QUEUE_FOREVER == ms_wait) {
				pthread_cond_wait(&q->CV, &q->LOCK);
			}
			else if (CF_QUEUE_NOWAIT == ms_wait) {
				pthread_mutex_unlock(&q->LOCK);
				return(CF_QUEUE_EMPTY);
			}
			else {
				pthread_cond_timedwait(&q->CV, &q->LOCK, &tp);
				if (CF_Q_EMPTY(q)) {
					pthread_mutex_unlock(&q->LOCK);
					return(CF_QUEUE_EMPTY);
				}
			}
		}
	} else if (CF_Q_EMPTY(q))
		return(CF_QUEUE_EMPTY);

	memcpy(buf, CF_Q_ELEM_PTR(q,q->read_offset), q->elementsz);
	q->read_offset++;
	
	// interesting idea - this probably keeps the cache fresher
	// because the queue is fully empty just make it all zero
	if (q->read_offset == q->write_offset) {
		q->read_offset = q->write_offset = 0;
	}

	/* FIXME blow a gasket */
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK))) {
		// cf_info(CF_QUEUE, "queue pop failed");
		return(-1);
	}

	return(0);
}


void
cf_queue_delete_offset(cf_queue *q, uint index)
{
	index %= q->allocsz;
	uint r_index = q->read_offset % q->allocsz;
	uint w_index = q->write_offset % q->allocsz;
	
	// assumes index is validated!
	
	// if we're deleting the one at the head, just increase the readoffset
	if (index == r_index) {
		q->read_offset++;
		return;
	}
	// if we're deleting the tail just decrease the write offset
	if (w_index && (index == w_index - 1)) {
		q->write_offset--;
		return;
	}
	// and the memory copy is overlapping, so must use memmove
	if (index > r_index) {
		memmove( &q->queue[ (r_index + 1) * q->elementsz ],
			     &q->queue[ r_index * q->elementsz ],
				 (index - r_index) * q->elementsz );
		q->read_offset++;
		return;
	}
	
	if (index < w_index) {
		memmove( &q->queue[ index * q->elementsz ],
			     &q->queue[ (index + 1) * q->elementsz ],
			     (w_index - index - 1) * q->elementsz);
		q->write_offset--;
		return;
	}
	
	// cf_debug(CF_QUEUE,"QUEUE_DELETE_OFFSET: FAIL FAIL FAIL FAIL");

}
	
	

// Iterate over all queue members calling the callback

int 
cf_queue_reduce(cf_queue *q,  cf_queue_reduce_fn cb, void *udata)
{
	if (NULL == q)
		return(-1);

	/* FIXME error checking */
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
		return(-1);

	if (CF_Q_SZ(q)) {
		
		// it would be faster to have a local variable to hold the index,
		// and do it in bytes or something, but a delete
		// will change the read and write offset, so this is simpler for now
		// can optimize if necessary later....
		
		for (uint i = q->read_offset ; 
			 i < q->write_offset ;
			 i++)
		{
			
			int rv = cb(CF_Q_ELEM_PTR(q, i), udata);
			
			// rv == 0 i snormal case, just increment to next point
			if (rv == -1) {
				break; // found what it was looking for
			}
			else if (rv == -2) { // delete!
				cf_queue_delete_offset(q, i);
				goto Found;
			}
		};
	}
	
Found:	
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK))) {
		fprintf(stderr, "unlock failed\n");
		return(-1);
	}

	return(0);
	
}

// Special case: delete elements from the queue
// pass 'true' as the 'only_one' parameter if you know there can be only one element
// with this value on the queue

int
cf_queue_delete(cf_queue *q, void *buf, bool only_one)
{
	if (NULL == q)
		return(CF_QUEUE_ERR);

	/* FIXME error checking */
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
		return(CF_QUEUE_ERR);

	bool found = false;
	
	if (CF_Q_SZ(q)) {

		for (uint i = q->read_offset ; 
			 i < q->write_offset ;
			 i++)
		{
			
			int rv = memcmp(CF_Q_ELEM_PTR(q,i), buf, q->elementsz);
			
			if (rv == 0) { // delete!
				cf_queue_delete_offset(q, i);
				found = true;
				if (only_one == true)	goto Done;
			}
		};
	}

Done:
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK))) {
		fprintf(stderr, "unlock failed\n");
		return(-1);
	}

	if (found == false)
		return(CF_QUEUE_EMPTY);
	else
		return(CF_QUEUE_OK);
}


//
// Priority queue implementation
//

cf_queue_priority *
cf_queue_priority_create(size_t elementsz, bool threadsafe)
{
	cf_queue_priority *q = malloc(sizeof(cf_queue_priority));
	if (!q)	return(0);
	
	q->threadsafe = threadsafe;
	q->low_q = cf_queue_create(elementsz, false);
	if (!q->low_q) 		goto Fail1;
	q->medium_q = cf_queue_create(elementsz, false);
	if (!q->medium_q)	goto Fail2;
	q->high_q = cf_queue_create(elementsz, false);
	if (!q->high_q)		goto Fail3;
	
	if (threadsafe == false)
		return(q);
	
	if (0 != pthread_mutex_init(&q->LOCK, NULL))
		goto Fail4;

	if (0 != pthread_cond_init(&q->CV, NULL))
		goto Fail5;
	
	return(q);
	
Fail5:	
	pthread_mutex_destroy(&q->LOCK);
Fail4:	
	cf_queue_destroy(q->high_q);
Fail3:	
	cf_queue_destroy(q->medium_q);
Fail2:	
	cf_queue_destroy(q->low_q);
Fail1:	
	free(q);
	return(0);
}

void 
cf_queue_priority_destroy(cf_queue_priority *q)
{
	cf_queue_destroy(q->high_q);
	cf_queue_destroy(q->medium_q);
	cf_queue_destroy(q->low_q);
	if (q->threadsafe) {
		pthread_mutex_destroy(&q->LOCK);
		pthread_cond_destroy(&q->CV);
	}
	free(q);
}

int 
cf_queue_priority_push(cf_queue_priority *q, void *ptr, int pri)
{
	
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
			return(-1);
	
	int rv;
	if (pri == CF_QUEUE_PRIORITY_HIGH)
		rv = cf_queue_push(q->high_q, ptr);
	else if (pri == CF_QUEUE_PRIORITY_MEDIUM)
		rv = cf_queue_push(q->medium_q, ptr);
	else if (pri == CF_QUEUE_PRIORITY_LOW)
		rv = cf_queue_push(q->low_q, ptr);
	else {
		// cf_warning(CF_QUEUE, "SERIOUS! bad priority %d passed into queue priority push",pri);
		rv = -1;
	}

	if (rv == 0 && q->threadsafe)
		pthread_cond_signal(&q->CV);

	/* FIXME blow a gasket */
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK)))
		return(-1);

		
	return(rv);	
}

int 
cf_queue_priority_pop(cf_queue_priority *q, void *buf, int ms_wait)
{
	if (q->threadsafe && (0 != pthread_mutex_lock(&q->LOCK)))
			return(-1);

	struct timespec tp;
	if (ms_wait > 0) {
		clock_gettime( CLOCK_REALTIME, &tp); 
		tp.tv_sec += ms_wait / 1000;
		tp.tv_nsec += (ms_wait % 1000) * 1000000;
		if (tp.tv_nsec > 1000000000) {
			tp.tv_nsec -= 1000000000;
			tp.tv_sec++;
		}
	}

	if (q->threadsafe) {
		while (CF_Q_PRI_EMPTY(q)) {
			if (CF_QUEUE_FOREVER == ms_wait) {
				pthread_cond_wait(&q->CV, &q->LOCK);
			}
			else if (CF_QUEUE_NOWAIT == ms_wait) {
				pthread_mutex_unlock(&q->LOCK);
				return(CF_QUEUE_EMPTY);
			}
			else {
				pthread_cond_timedwait(&q->CV, &q->LOCK, &tp);
				if (CF_Q_PRI_EMPTY(q)) {
					pthread_mutex_unlock(&q->LOCK);
					return(CF_QUEUE_EMPTY);
				}
			}
		}
	}
	
	int rv;
	if (CF_Q_SZ(q->high_q))
		rv = cf_queue_pop(q->high_q, buf, 0);
	else if (CF_Q_SZ(q->medium_q))
		rv = cf_queue_pop(q->medium_q, buf, 0);
	else if (CF_Q_SZ(q->low_q))
		rv = cf_queue_pop(q->low_q, buf, 0);
	else rv = CF_QUEUE_EMPTY;
		
	if (q->threadsafe && (0 != pthread_mutex_unlock(&q->LOCK)))
		return(-1);

		
	return(rv);
}

int
cf_queue_priority_sz(cf_queue_priority *q)
{
	int rv = 0;
	if (q->threadsafe)
		pthread_mutex_lock(&q->LOCK);
	rv += cf_queue_sz(q->high_q);
	rv += cf_queue_sz(q->medium_q);
	rv += cf_queue_sz(q->low_q);
	if (q->threadsafe)
		pthread_mutex_unlock(&q->LOCK);
	return(rv);
}



//
// Test code
// it's always really annoying to have a queue that malfunctions in some small way
// so best to have a pretty serious torture test suite
//


#define TEST1_SZ 400
#define TEST1_INTERVAL 10

void *
cf_queue_test_1_write(void *arg)
{
	cf_queue *q = (cf_queue *) arg;
	
	for (int i=0; i<TEST1_SZ;i++) {
		
		usleep(TEST1_INTERVAL * 1000);
		
		int rv;
		rv = cf_queue_push(q, &i);
		if (0 != rv) {
			fprintf(stderr, "queue push failed: error %d",rv);
			return((void *)-1);
		}
	}
	
	return((void *)0);
	
}	

void *
cf_queue_test_1_read(void *arg)
{
	cf_queue *q = (cf_queue *) arg;
	
	for (int i=0;i<TEST1_SZ;i++) {
		
		// sleep twice as long as the inserter, to test overflow
		usleep(TEST1_INTERVAL * 1000 * 2);
		
		int  v = -1;
		int rv = cf_queue_pop(q, &v, CF_QUEUE_FOREVER);
		if (rv != CF_QUEUE_OK) {
			fprintf(stderr, "cf_queue_test1: pop error %d",rv);
			return((void *) -1);
		}
		if (v != i) {
			fprintf(stderr, "cf_queue_test1: pop value error: %d should be %d",v,i);
			return((void *) -1);
		}
		
	}
	return((void *) 0);	
}


int
cf_queue_test_1()
{
	pthread_t 	write_th;
	pthread_t     read_th;
	cf_queue    *q;
	
	q = cf_queue_create(sizeof(int), true);
	
	pthread_create( & write_th, 0, cf_queue_test_1_write, q);
	
	pthread_create( & read_th, 0, cf_queue_test_1_read, q);
	
	void *th_return;
	
	if (0 != pthread_join(write_th, &th_return)) {
		fprintf(stderr, "queue test 1: could not join1 %d\n",errno);
		return(-1);
	}
	
	if (0 != th_return) {
		fprintf(stderr, "queue test 1: returned error %p\n",th_return);
		return(-1);
	}
	
	if (0 != pthread_join(read_th, &th_return)) {
		fprintf(stderr, "queue test 1: could not join2 %d\n",errno);
		return(-1);
	}
	
	if (0 != th_return) {
		fprintf(stderr, "queue test 1: returned error 2 %p\n",th_return);
		return(-1);
	}
	
	cf_queue_destroy(q);
	
	return(0);
}

int
cf_queue_test()
{
	if (0 != cf_queue_test_1()) {
		fprintf(stderr, "CF QUEUE TEST ONE FAILED\n");
		return(-1);
	}
	
	return(0);
}

