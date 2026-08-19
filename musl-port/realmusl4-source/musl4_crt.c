/* Musl integration source. */
extern int main(void); extern void _exit(int);
void _start(void){int rc=main();_exit(rc);}
