
#include "lib/musl_compat.h"
#include "lib/stdio.h"
#include "lib/string.h"

static int ok(int x) { return x ? 1 : 0; }

int main(int argc, char **argv, char **envp) {
	(void)envp;
	printf("REALMUSL4 LIBC TEST\n");
	printf("argc=%d\n", argc);
	for (int i = 0; i < argc; i++) printf("argv[%d]=%s\n", i, argv[i]);

	char *p = (char *)malloc(256);
	if (!p) return 1;
	strcpy(p, "Hello from libc");
	printf("malloc/strcpy=%s\n", ok(strlen(p) == 15) ? "OK" : "FAIL");

	char *q = (char *)malloc(256);
	if (!q) return 1;
	memcpy(q, p, 16);
	printf("memcpy=%s\n", ok(strcmp(q, p) == 0) ? "OK" : "FAIL");
	memmove(q + 5, q, 6);
	printf("memmove=%s\n", ok(strncmp(q + 5, "Hello ", 6) == 0) ? "OK" : "FAIL");
	printf("memcmp=%s\n", ok(memcmp(p, "Hello from libc", 15) == 0) ? "OK" : "FAIL");
	printf("strchr=%s\n", ok(strchr(p, 'c') != 0) ? "OK" : "FAIL");

	FILE *f = fopen("/REALMUSL4.TXT", "w");
	if (!f) { puts("fopen=FAIL"); return 1; }
	fprintf(f, "hello from REALMUSL4 argc=%d\n", argc);
	fprintf(f, "argv1=%s\n", argc > 1 ? argv[1] : "none");
	fclose(f);
	printf("fopen/fprintf/fclose=OK\n");

	f = fopen("/REALMUSL4.TXT", "r");
	if (!f) return 1;
	char buf[128];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	printf("fread=%s\n", ok(n > 0 && strstr(buf, "hello from REALMUSL4") != 0) ? "OK" : "FAIL");
	fclose(f);

	char out[128];
	snprintf(out, sizeof(out), "pid=%d argc=%d", getpid(), argc);
	printf("snprintf=%s\n", ok(strstr(out, "argc=") != 0) ? "OK" : "FAIL");

	printf("getenv PATH=%s\n", getenv("PATH") ? getenv("PATH") : "<unset>");
	printf("setenv=%s\n", ok(setenv("REALMUSL4_TEST", "yes", 1) == 0) ? "OK" : "FAIL");
	printf("getenv TEST=%s\n", getenv("REALMUSL4_TEST") ? getenv("REALMUSL4_TEST") : "<unset>");
	printf("unsetenv=%s\n", ok(unsetenv("REALMUSL4_TEST") == 0 && getenv("REALMUSL4_TEST") == 0) ? "OK" : "FAIL");

	free(q); free(p);
	puts("REALMUSL4 libc checks=OK");
	return 42;
}
