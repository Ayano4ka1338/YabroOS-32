#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int musl_test_02(void) {
	long p = sysconf(_SC_PAGESIZE);
	if (p <= 0) return 10;
	void *m = mmap(0, (size_t)p, PROT_READ|PROT_WRITE,
				   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED) return 11;
	strcpy((char *)m, "mmap-ok");
	puts((char *)m);
	if (mprotect(m, (size_t)p, PROT_READ) != 0) return 12;
	if (munmap(m, (size_t)p) != 0) return 13;
	return 0;
}
