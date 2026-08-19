#!/bin/sh
qemu-system-x86_64 -cdrom yabroos-32.iso -m 256M -vga std -rtc base=localtime,clock=host
