/*
 * queue.h
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

#pragma once

//==========================================================
// Includes.
//

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


//==========================================================
// Typedefs & constants.
//

typedef struct queue_s {
	uint32_t alloc_sz;          // number of elements currently allocated
	uint32_t read_offset;       // head of queue
	uint32_t write_offset;      // tail of queue - write is always >= read
	size_t ele_size;            // size of (every) element in bytes
	pthread_mutex_t lock;       // the lock - used in thread-safe mode
	uint8_t* elements;          // the elements' bytes
} queue;


//==========================================================
// Public API.
//

queue* queue_create(size_t ele_size);
void queue_destroy(queue* q);
uint32_t queue_sz(queue* q);
bool queue_push(queue* q, const void* ele_ptr);
bool queue_pop(queue* q, void* ele_ptr);
