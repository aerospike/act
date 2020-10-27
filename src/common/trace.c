/*
 * trace.c
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

#include "trace.h"

#include <execinfo.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


//==========================================================
// Typedefs & constants.
//

typedef void (*action_t)(int sig, siginfo_t* info, void* ctx);

#define MAX_BACKTRACE_DEPTH 50


//==========================================================
// Forward declarations.
//

static void act_sig_handle_abort(int sig_num, siginfo_t* info, void* ctx);
static void act_sig_handle_bus(int sig_num, siginfo_t* info, void* ctx);
static void act_sig_handle_fpe(int sig_num, siginfo_t* info, void* ctx);
static void act_sig_handle_ill(int sig_num, siginfo_t* info, void* ctx);
static void act_sig_handle_segv(int sig_num, siginfo_t* info, void* ctx);

static void reraise_signal(int sig_num);
static void set_action(int sig_num, action_t act);
static void set_handler(int sig_num, sighandler_t hand);

extern char __executable_start;


//==========================================================
// Inlines & macros.
//

// Macro instead of inline just to be sure it can't affect the stack?
#define PRINT_BACKTRACE() \
do { \
	void* bt[MAX_BACKTRACE_DEPTH]; \
	int sz = backtrace(bt, MAX_BACKTRACE_DEPTH); \
	\
	char trace[MAX_BACKTRACE_DEPTH * 20]; \
	int off = 0; \
	\
	for (int i = 0; i < sz; i++) { \
		off += snprintf(trace + off, sizeof(trace) - off, " 0x%lx", \
				(uint64_t)bt[i]); \
	} \
	\
	printf("stacktrace: found %d frames:%s offset 0x%lx\n", sz, trace, \
			(uint64_t)&__executable_start); \
	\
	char** syms = backtrace_symbols(bt, sz); \
	\
	if (syms != NULL) { \
		for (int i = 0; i < sz; ++i) { \
			printf("stacktrace: frame %d: %s\n", i, syms[i]); \
		} \
	} \
	else { \
		printf("stacktrace: found no symbols\n"); \
	} \
	\
	fflush(stdout); \
} while (0);


//==========================================================
// Public API.
//

void
signal_setup()
{
	set_action(SIGABRT, act_sig_handle_abort);
	set_action(SIGBUS, act_sig_handle_bus);
	set_action(SIGFPE, act_sig_handle_fpe);
	set_action(SIGILL, act_sig_handle_ill);
	set_action(SIGSEGV, act_sig_handle_segv);
}


//==========================================================
// Local helpers - signal handlers.
//

static void
act_sig_handle_abort(int sig_num, siginfo_t* info, void* ctx)
{
	printf("SIGABRT received\n");
	PRINT_BACKTRACE();
	reraise_signal(sig_num);
}

static void
act_sig_handle_bus(int sig_num, siginfo_t* info, void* ctx)
{
	printf("SIGBUS received\n");
	PRINT_BACKTRACE();
	reraise_signal(sig_num);
}

static void
act_sig_handle_fpe(int sig_num, siginfo_t* info, void* ctx)
{
	printf("SIGFPE received\n");
	PRINT_BACKTRACE();
	reraise_signal(sig_num);
}

static void
act_sig_handle_ill(int sig_num, siginfo_t* info, void* ctx)
{
	printf("SIGILL received\n");
	PRINT_BACKTRACE();
	reraise_signal(sig_num);
}

static void
act_sig_handle_segv(int sig_num, siginfo_t* info, void* ctx)
{
	printf("SIGSEGV received\n");
	PRINT_BACKTRACE();
	reraise_signal(sig_num);
}


//==========================================================
// Local helpers - signal handling.
//

static void
reraise_signal(int sig_num)
{
	set_handler(sig_num, SIG_DFL);
	raise(sig_num);
}

static void
set_action(int sig_num, action_t act)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	sa.sa_sigaction = act;
	sigemptyset(&sa.sa_mask);
	// SA_SIGINFO prefers sa_sigaction over sa_handler.
	sa.sa_flags = SA_RESTART | SA_SIGINFO;

	if (sigaction(sig_num, &sa, NULL) < 0) {
		printf("ERROR: could not register signal handler for %d\n", sig_num);
		fflush(stdout);
		_exit(-1);
	}
}

static void
set_handler(int sig_num, sighandler_t hand)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = hand;
	sigemptyset(&sa.sa_mask);
	// No SA_SIGINFO; use sa_handler.
	sa.sa_flags = SA_RESTART;

	if (sigaction(sig_num, &sa, NULL) < 0) {
		printf("ERROR: could not register signal handler for %d\n", sig_num);
		fflush(stdout);
		_exit(-1);
	}
}
