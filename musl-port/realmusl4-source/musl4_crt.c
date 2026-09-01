
#include <stdint.h>

extern int main(int argc, char **argv, char **envp);
extern void _exit(int);
char **__yabroos_envp;

__attribute__((noreturn, naked))
void _start(void) {
	__asm__ volatile (
		"mov (%rsp), %rdi\n"
		"lea 8(%rsp), %rsi\n"
		"lea 16(%rsp,%rdi,8), %rdx\n"
		"mov %rdx, __yabroos_envp(%rip)\n"
		"and $-16, %rsp\n"
		"call main\n"
		"mov %eax, %edi\n"
		"call _exit\n"
		"hlt\n"
	);
}
