# REALMUSL4 source

This directory contains the source used to build `REALMUSL4.ELF`.

## Build

On Debian/Ubuntu, install GCC and binutils, then run:

```sh
./build-realmusl4.sh
```

The script builds `REALMUSL4.ELF` from `realmusl4.c`, the CRT, and the small libc support sources.

`REALMUSL4.ELF` in the project root is the runtime artifact.
