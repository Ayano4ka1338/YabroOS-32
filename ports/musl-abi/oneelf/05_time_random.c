#include <stdio.h>
#include <time.h>
#include <sys/random.h>
#include <stdint.h>
int musl_test_05(void) {
	struct timespec ts;
	uint64_t r = 0;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 40;
	if (getrandom(&r, sizeof(r), 0) != (long)sizeof(r)) return 41;
	printf("time-random-ok=%lld\n", (long long)ts.tv_sec);
	return 0;
}
