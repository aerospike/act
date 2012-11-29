/*
 *	salt.c
 */


//==========================================================
// Includes
//

#include <dirent.h>
#include <execinfo.h>	// for debugging
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>		// for debugging
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/fs.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <sys/ioctl.h>


//==========================================================
// Constants
//

const uint32_t NUM_SALT_BUFFERS = 256;
const uint32_t NUM_SALT_THREADS = 8;
const uint32_t NUM_ZERO_THREADS = 8;
const uint32_t LARGE_BLOCK_BYTES = 1024 * 128;
const uint32_t RAND_SEED_SIZE = 64;

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 00040000
#endif


//==========================================================
// Typedefs
//

typedef struct _salter {
	uint8_t* p_buffer;
	pthread_mutex_t lock;
	uint32_t stamp;
} salter;


//==========================================================
// Globals
//

static char* g_device_name = NULL;
static uint64_t g_num_large_blocks = 0;
static salter* g_salters = NULL;
static uint8_t* g_p_zero_buffer = NULL;
static uint64_t g_blocks_per_salt_thread = 0;
static uint64_t g_blocks_per_zero_thread = 0;
static uint64_t g_extra_blocks_to_zero = 0;
static uint64_t g_extra_blocks_to_salt = 0;
static uint64_t g_extra_bytes_to_zero = 0;


//==========================================================
// Forward Declarations
//

static void*			run_salt(void* pv_n);
static void*			run_zero(void* pv_n);

static inline uint8_t*	cf_valloc(size_t size);
static bool				create_salters();
static bool				create_zero_buffer();
static void				destroy_salters();
static bool				discover_num_blocks();
static inline int		fd_get();
static bool				rand_fill(uint8_t* p_buffer, uint32_t size);
static bool				rand_seed();
static void				set_scheduler();

static void				as_sig_handle_segv(int sig_num);
static void				as_sig_handle_term(int sig_num);


//==========================================================
// Main
//

int main(int argc, char* argv[]) {
	signal(SIGSEGV, as_sig_handle_segv);
	signal(SIGTERM , as_sig_handle_term);

	if (argc != 2) {
		fprintf(stdout, "usage: salt [device name]\n");
		exit(0);
	}

	char device_name[strlen(argv[1]) + 1];

	strcpy(device_name, argv[1]);
	g_device_name = device_name;

	set_scheduler();

	if (! discover_num_blocks()) {
		exit(-1);
	}

	//------------------------
	// Begin zeroing.

	fprintf(stdout, "cleaning device %s\n", g_device_name);

	if (! create_zero_buffer()) {
		exit(-1);
	}

	pthread_t zero_threads[NUM_ZERO_THREADS];

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		if (pthread_create(&zero_threads[n], NULL, run_zero,
				(void*)(uint64_t)n)) {
			fprintf(stdout, "ERROR: creating zero thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_ZERO_THREADS; n++) {
		void* pv_value;

		pthread_join(zero_threads[n], &pv_value);
	}

	free(g_p_zero_buffer);

	//------------------------
	// Begin salting.

	fprintf(stdout, "salting device %s\n", g_device_name);

	srand(time(NULL));

	if (! rand_seed()) {
		exit(-1);
	}

	salter salters[NUM_SALT_BUFFERS];

	g_salters = salters;

	if (! create_salters()) {
		exit(-1);
	}

	pthread_t salt_threads[NUM_SALT_THREADS];

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		if (pthread_create(&salt_threads[n], NULL, run_salt,
				(void*)(uint64_t)n)) {
			fprintf(stdout, "ERROR: creating salt thread %" PRIu32 "\n", n);
			exit(-1);
		}
	}

	for (uint32_t n = 0; n < NUM_SALT_THREADS; n++) {
		void* pv_value;

		pthread_join(salt_threads[n], &pv_value);
	}

	destroy_salters();

	return 0;
}


//==========================================================
// Thread "Run" Functions
//

//------------------------------------------------
// Runs in all (NUM_SALT_THREADS) salt_threads,
// salts a portion of the device.
//
static void* run_salt(void* pv_n) {
	uint32_t n = (uint32_t)(uint64_t)pv_n;

	uint64_t offset = n * g_blocks_per_salt_thread * LARGE_BLOCK_BYTES;
	uint64_t blocks_to_salt = g_blocks_per_salt_thread;
	uint64_t progress_blocks = 0;
	bool last_thread = n + 1 == NUM_SALT_THREADS;

	if (last_thread) {
		blocks_to_salt += g_extra_blocks_to_salt;
		progress_blocks = blocks_to_salt / 100;

		if (! progress_blocks) {
			progress_blocks = 1;
		}
	}

//	fprintf(stdout, "thread %d: blks-to-salt = %" PRIu64 ", prg-blks = %"
//		PRIu64 "\n", n, blocks_to_salt, progress_blocks);

	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: open in salt thread %" PRIu32 "\n", n);
		// TODO - what?
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		close(fd);
		fprintf(stdout, "ERROR: seek in salt thread %" PRIu32 "\n", n);
		// TODO - what?
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_salt; b++) {
		salter* p_salter = &g_salters[rand() % NUM_SALT_BUFFERS];

		pthread_mutex_lock(&p_salter->lock);

		*(uint32_t*)p_salter->p_buffer = p_salter->stamp++;

		if (write(fd, p_salter->p_buffer, LARGE_BLOCK_BYTES) !=
				(ssize_t)LARGE_BLOCK_BYTES) {
			pthread_mutex_unlock(&p_salter->lock);
			fprintf(stdout, "ERROR: write in salt thread %" PRIu32 "\n", n);
			// TODO - what?
			break;
		}

		pthread_mutex_unlock(&p_salter->lock);

		if (progress_blocks && ! (b % progress_blocks)) {
			fprintf(stdout, ".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		fprintf(stdout, "\n");
	}

	close(fd);

	return NULL;
}

//------------------------------------------------
// Runs in all (NUM_ZERO_THREADS) zero_threads,
// zeros a portion of the device.
//
static void* run_zero(void* pv_n) {
	uint32_t n = (uint32_t)(uint64_t)pv_n;

	uint64_t offset = n * g_blocks_per_zero_thread * LARGE_BLOCK_BYTES;
	uint64_t blocks_to_zero = g_blocks_per_zero_thread;
	uint64_t progress_blocks = 0;
	bool last_thread = n + 1 == NUM_ZERO_THREADS;

	if (last_thread) {
		blocks_to_zero += g_extra_blocks_to_zero;
		progress_blocks = blocks_to_zero / 100;

		if (! progress_blocks) {
			progress_blocks = 1;
		}
	}

//	fprintf(stdout, "thread %d: blks-to-zero = %" PRIu64 ", prg-blks = %"
//		PRIu64 "\n", n, blocks_to_zero, progress_blocks);

	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: open in zero thread %" PRIu32 "\n", n);
		// TODO - what?
		return NULL;
	}

	if (lseek(fd, offset, SEEK_SET) != offset) {
		close(fd);
		fprintf(stdout, "ERROR: seek in zero thread %" PRIu32 "\n", n);
		// TODO - what?
		return NULL;
	}

	for (uint64_t b = 0; b < blocks_to_zero; b++) {
		if (write(fd, g_p_zero_buffer, LARGE_BLOCK_BYTES) !=
				(ssize_t)LARGE_BLOCK_BYTES) {
			fprintf(stdout, "ERROR: write in zero thread %" PRIu32 "\n", n);
			// TODO - what?
			break;
		}

		if (progress_blocks && ! (b % progress_blocks)) {
			fprintf(stdout, ".");
			fflush(stdout);
		}
	}

	if (progress_blocks) {
		fprintf(stdout, "\n");
	}

	if (last_thread) {
		if (write(fd, g_p_zero_buffer, g_extra_bytes_to_zero) !=
				(ssize_t)g_extra_bytes_to_zero) {
			fprintf(stdout, "ERROR: write in zero thread %" PRIu32 "\n", n);
		}
	}

	close(fd);

	return NULL;
}


//==========================================================
// Helpers
//

//------------------------------------------------
// Aligned memory allocation.
//
static inline uint8_t* cf_valloc(size_t size) {
	void* pv;

	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
}

//------------------------------------------------
// Allocate large block sized salt buffers, etc.
//
static bool create_salters() {
	for (uint32_t n = 0; n < NUM_SALT_BUFFERS; n++) {
		if (! (g_salters[n].p_buffer = cf_valloc(LARGE_BLOCK_BYTES))) {
			fprintf(stdout, "ERROR: salt buffer %" PRIu32 " cf_valloc()\n", n);
			return false;
		}

		if (! rand_fill(g_salters[n].p_buffer, LARGE_BLOCK_BYTES)) {
			return false;
		}

		pthread_mutex_init(&g_salters[n].lock, NULL);
	}

	return true;
}

//------------------------------------------------
// Allocate and zero one large block sized buffer.
//
static bool create_zero_buffer() {
	g_p_zero_buffer = cf_valloc(LARGE_BLOCK_BYTES);

	if (! g_p_zero_buffer) {
		fprintf(stdout, "ERROR: zero buffer cf_valloc()\n");
		return false;
	}

	memset(g_p_zero_buffer, 0, LARGE_BLOCK_BYTES);

	return true;
}

//------------------------------------------------
// Destroy large block sized salt buffers.
//
static void destroy_salters() {
	for (uint32_t n = 0; n < NUM_SALT_BUFFERS; n++) {
		free(g_salters[n].p_buffer);
		pthread_mutex_destroy(&g_salters[n].lock);
	}
}

//------------------------------------------------
// Discover device storage capacity.
//
static bool discover_num_blocks() {
	int fd = fd_get();

	if (fd == -1) {
		fprintf(stdout, "ERROR: opening device %s\n", g_device_name);
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	close(fd);

	g_num_large_blocks = device_bytes / LARGE_BLOCK_BYTES;
	g_extra_bytes_to_zero = device_bytes % LARGE_BLOCK_BYTES;

	g_blocks_per_zero_thread = g_num_large_blocks / NUM_ZERO_THREADS;
	g_blocks_per_salt_thread = g_num_large_blocks / NUM_SALT_THREADS;

	g_extra_blocks_to_zero = g_num_large_blocks % NUM_ZERO_THREADS;
	g_extra_blocks_to_salt = g_num_large_blocks % NUM_SALT_THREADS;

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks\n",
		g_device_name, device_bytes, g_num_large_blocks);

	return true;
}

//------------------------------------------------
// Get a file descriptor.
//
static inline int fd_get() {
	return open(g_device_name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);
}

//------------------------------------------------
// Fill a buffer with random bits.
//
static bool rand_fill(uint8_t* p_buffer, uint32_t size) {
	if (RAND_bytes(p_buffer, size) != 1) {
		fprintf(stdout, "ERROR: RAND_bytes() failed\n");
		return false;
	}

	return true;
}

//------------------------------------------------
// Seed for random fill.
//
static bool rand_seed() {
	int fd = open("/dev/urandom", O_RDONLY);

	if (fd == -1) {
		fprintf(stdout, "ERROR: can't open /dev/urandom\n");
		return false;
	}

	uint8_t seed_buffer[RAND_SEED_SIZE];
	ssize_t read_result = read(fd, seed_buffer, RAND_SEED_SIZE);

	if (read_result != (ssize_t)RAND_SEED_SIZE) {
		close(fd);
		fprintf(stdout, "ERROR: can't seed random number generator\n");
		return false;
	}

	close(fd);
	RAND_seed(seed_buffer, read_result);

	return true;
}

//------------------------------------------------
// Set device's system block scheduler to noop.
//
static void set_scheduler() {
	const char* p_slash = strrchr(g_device_name, '/');
	const char* device_tag = p_slash ? p_slash + 1 : g_device_name;

	char scheduler_file_name[128];

	strcpy(scheduler_file_name, "/sys/block/");
	strcat(scheduler_file_name, device_tag);
	strcat(scheduler_file_name, "/queue/scheduler");

	FILE* scheduler_file = fopen(scheduler_file_name, "w");

	if (! scheduler_file) {
		fprintf(stdout, "ERROR: couldn't open %s\n", scheduler_file_name);
		return;
	}

	if (fwrite("noop", 4, 1, scheduler_file) != 1) {
		fprintf(stdout, "ERROR: writing noop to %s\n", scheduler_file_name);
	}

	fclose(scheduler_file);
}


//==========================================================
// Debugging Helpers
//

static void as_sig_handle_segv(int sig_num) {
	fprintf(stdout, "Signal SEGV received: stack trace\n");

	void* bt[50];
	uint sz = backtrace(bt, 50);
	
	char** strings = backtrace_symbols(bt, sz);

	for (int i = 0; i < sz; ++i) {
		fprintf(stdout, "stacktrace: frame %d: %s\n", i, strings[i]);
	}

	free(strings);
	
	fflush(stdout);
	_exit(-1);
}

static void as_sig_handle_term(int sig_num) {
	fprintf(stdout, "Signal TERM received, aborting\n");

  	void* bt[50];
	uint sz = backtrace(bt, 50);

	char** strings = backtrace_symbols(bt, sz);

	for (int i = 0; i < sz; ++i) {
		fprintf(stdout, "stacktrace: frame %d: %s\n", i, strings[i]);
	}

	free(strings);

	fflush(stdout);
	_exit(0);
}
