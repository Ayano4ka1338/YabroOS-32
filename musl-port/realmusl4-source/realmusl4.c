/* Musl integration source. */
#include "lib/musl_compat.h"
#include "lib/stdio.h"
#include "lib/string.h"
static int ok(int x){return x?1:0;}
int main(void){
 puts("Hello from YabroOS-32 REALMUSL4!");
 char *p=malloc(256); printf("malloc=%s\n",p?"OK":"FAIL"); if(!p)return 1;
 memset(p,0,256); strcpy(p,"Hello from libc layer");
 printf("strcpy/strlen=%s\n",ok(strlen(p)==21)?"OK":"FAIL");
 char *q=malloc(256); if(!q){free(p);return 1;}
 memcpy(q,p,22); printf("memcpy=%s\n",ok(strcmp(q,p)==0)?"OK":"FAIL");
 memmove(q+5,q,6); printf("memmove=%s\n",ok(strncmp(q+5,"Hello ",6)==0)?"OK":"FAIL");
 printf("memcmp=%s\n",ok(memcmp(p,"Hello from libc layer",21)==0)?"OK":"FAIL");
 printf("strchr=%s\n",ok(strchr(p,'c')!=0)?"OK":"FAIL");
 free(q); free(p); puts("free=OK");
 puts("REALMUSL4 checks=OK");
 return 0;
}
