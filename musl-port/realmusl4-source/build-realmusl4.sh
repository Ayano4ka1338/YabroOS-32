#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
C='gcc -m64 -ffreestanding -nostdlib -static -no-pie -fno-pie -O2 -Wall -Wextra -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-tree-vectorize -fno-tree-slp-vectorize -march=x86-64 -mtune=generic'
$C -I. -Ilib -Iarch/x86_64 -c musl4_crt.c -o musl4_crt.o
$C -I. -Ilib -Iarch/x86_64 -c lib/musl_compat.c -o lib/musl_compat.o
$C -I. -Ilib -Iarch/x86_64 -c lib/stdio.c -o lib/stdio.o
$C -I. -Ilib -Iarch/x86_64 -c lib/string.c -o lib/string.o
$C -I. -Ilib -Iarch/x86_64 -c realmusl4.c -o realmusl4.o
$C -I. -Ilib -Iarch/x86_64 -c libc_test.c -o libc_test.o
$C -I. -Ilib -Iarch/x86_64 -c device_test.c -o device_test.o
ld -m elf_x86_64 -static -nostdlib -T linker.ld musl4_crt.o realmusl4.o lib/musl_compat.o lib/stdio.o lib/string.o -o REALMUSL4.ELF
ld -m elf_x86_64 -static -nostdlib -T linker.ld musl4_crt.o libc_test.o lib/musl_compat.o lib/stdio.o lib/string.o -o LIBC-TEST.ELF
ld -m elf_x86_64 -static -nostdlib -T linker.ld musl4_crt.o device_test.o lib/musl_compat.o lib/stdio.o lib/string.o -o DEV-TEST.ELF
