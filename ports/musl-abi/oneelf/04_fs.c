#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
int musl_test_04(void) {
	int fd = open("/dev/null", O_RDWR);
	if (fd < 0) return 30;
	struct stat st;
	if (fstat(fd, &st) != 0) return 31;
	if (write(fd, "x", 1) != 1) return 32;
	close(fd);
	puts("fs-openat-stat-ok");
	return 0;
}
