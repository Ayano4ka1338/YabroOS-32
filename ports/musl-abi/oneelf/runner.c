#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int musl_test_01(void);
extern int musl_test_02(void);
extern int musl_test_03(void);
extern int musl_test_04(void);
extern int musl_test_05(void);
extern int musl_test_06(void);
extern int musl_test_07(void);
extern int musl_test_08(void);

static void usage(const char *p) {
	dprintf(2, "usage: %s [all|hello|memory|thread|fs|time|fds|socketpair]\n", p);
}
int main(int argc, char **argv) {
	if (argc == 1 || !strcmp(argv[1], "all")) {
		int rc=0, r;
		r=musl_test_01(); if(r && !rc) rc=r;
		r=musl_test_02(); if(r && !rc) rc=r;
		r=musl_test_03(); if(r && !rc) rc=r;
		r=musl_test_04(); if(r && !rc) rc=r;
		r=musl_test_05(); if(r && !rc) rc=r;
		r=musl_test_06(); if(r && !rc) rc=r;
		r=musl_test_07(); if(r && !rc) rc=r;
		r=musl_test_08(); if(r && !rc) rc=r;
		return rc;
	}
	if (!strcmp(argv[1],"hello")) return musl_test_01();
	if (!strcmp(argv[1],"memory")) return musl_test_02();
	if (!strcmp(argv[1],"thread")) return musl_test_03();
	if (!strcmp(argv[1],"fs")) return musl_test_04();
	if (!strcmp(argv[1],"time")) return musl_test_05();
	if (!strcmp(argv[1],"fds")) return musl_test_06();
	if (!strcmp(argv[1],"socketpair")) return musl_test_07();
	if (!strcmp(argv[1],"tls")) return musl_test_08();
	usage(argv[0]); return 64;
}
