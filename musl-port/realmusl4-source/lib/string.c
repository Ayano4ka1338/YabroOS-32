
#include "string.h"
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
void *memset(void*d,int c,size_t n){unsigned char*p=d;for(size_t i=0;i<n;i++)p[i]=(unsigned char)c;return d;}
void *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memmove(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;if(a==b)return d;if(a<b){for(size_t i=0;i<n;i++)a[i]=b[i];}else{while(n){--n;a[n]=b[n];}}return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;}
char *strcpy(char*d,const char*s){char*r=d;while((*d++=*s++));return r;}
char *strncpy(char*d,const char*s,size_t n){char*r=d;size_t i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return r;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char*a,const char*b,size_t n){for(size_t i=0;i<n;i++){unsigned char x=a[i],y=b[i];if(x!=y)return x<y?-1:1;if(!x)return 0;}return 0;}
char *strchr(const char*s,int c){for(;*s;s++)if((unsigned char)*s==(unsigned char)c)return (char*)s;return c==0?(char*)s:0;}

char *strstr(const char*h,const char*n){if(!*n)return(char*)h;for(;*h;h++){const char*a=h,*b=n;while(*a&&*b&&*a==*b){a++;b++;}if(!*b)return(char*)h;}return 0;}
