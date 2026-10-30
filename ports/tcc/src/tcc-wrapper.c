#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

#define TCC_REAL_PATH "/usr/libexec/tcc.real"

int main(int argc, char **argv)
{
    int add_static = 0;
    char **next_argv;
    int out = 0;

    if (argc < 1 || !argv) {
        fputs("tcc: invalid argument vector\n", stderr);
        return 2;
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-shared") == 0 || strcmp(argv[i], "-static") == 0) {
            add_static = 0;
        }
    }

    next_argv = calloc((size_t)argc + 3, sizeof(*next_argv));
    if (!next_argv) {
        fputs("tcc: out of memory\n", stderr);
        return 2;
    }

    next_argv[out++] = (char *)TCC_REAL_PATH;
    if (add_static) {
        next_argv[out++] = (char *)"-static";
    }
    for (int i = 1; i < argc; ++i) {
        next_argv[out++] = argv[i];
    }
    next_argv[out] = NULL;

    execve(TCC_REAL_PATH, next_argv, environ);
    fprintf(stderr, "tcc: cannot start %s: %s\n", TCC_REAL_PATH, strerror(errno));
    free(next_argv);
    return 127;
}
