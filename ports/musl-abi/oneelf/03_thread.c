#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>

static __thread uint64_t tls_value;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int shared;

static void *worker(void *arg) {
	(void)arg;
	tls_value = 0x123456789abcdef0ULL;
	pthread_mutex_lock(&lock);
	shared = (tls_value == 0x123456789abcdef0ULL);
	pthread_mutex_unlock(&lock);
	return 0;
}

int musl_test_03(void) {
	pthread_t t;
	if (pthread_create(&t, 0, worker, 0) != 0) return 20;
	if (pthread_join(t, 0) != 0) return 21;
	printf("tls-pthread-ok=%d\n", shared);
	return shared ? 0 : 22;
}
