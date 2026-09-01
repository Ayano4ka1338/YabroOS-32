#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
int musl_test_07(void) {
	int s[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) != 0) return 60;
	if (write(s[0], "ok", 2) != 2) return 61;
	char b[3] = {0};
	if (read(s[1], b, 2) != 2) return 62;
	close(s[0]); close(s[1]);
	puts(b);
	return 0;
}
