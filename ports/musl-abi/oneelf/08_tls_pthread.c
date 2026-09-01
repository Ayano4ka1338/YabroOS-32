#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

static __thread uint64_t tls_value;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready;
static int worker_ok;

static void *worker(void *arg) {
	(void)arg;
	tls_value = 0x1122334455667788ULL;
	if (pthread_mutex_lock(&lock) != 0) return (void *)1;
	worker_ok = (tls_value == 0x1122334455667788ULL);
	ready = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
	return 0;
}

int musl_test_08(void) {
	pthread_t t;
	tls_value = 0xaabbccddeeff0011ULL;
	if (pthread_create(&t, 0, worker, 0) != 0) return 70;
	if (pthread_mutex_lock(&lock) != 0) return 71;
	while (!ready) {
		if (pthread_cond_wait(&cond, &lock) != 0) {
			pthread_mutex_unlock(&lock);
			return 72;
		}
	}
	int ok = worker_ok && (tls_value == 0xaabbccddeeff0011ULL);
	pthread_mutex_unlock(&lock);
	if (pthread_join(t, 0) != 0) return 73;
	printf("tls-pthread-cond-ok=%d\n", ok);
	return ok ? 0 : 74;
}
