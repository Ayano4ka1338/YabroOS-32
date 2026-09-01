#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
int musl_test_06(void) {
	int p[2];
	if (pipe2(p, O_CLOEXEC) != 0) return 50;
	if (write(p[1], "x", 1) != 1) return 51;
	struct pollfd f = { .fd = p[0], .events = POLLIN };
	if (poll(&f, 1, 0) <= 0) return 52;
	char c;
	if (read(p[0], &c, 1) != 1 || c != 'x') return 53;
	close(p[0]); close(p[1]);
	puts("fd-poll-pipe-ok");
	return 0;
}
