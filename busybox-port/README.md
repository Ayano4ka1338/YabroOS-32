# YabroOS-32 BusyBox bring-up

The kernel/userspace ABI is now at the point where a real shell can exercise
fork/exec, pipes, dup2 and writable RAM-backed files. The next step is to build
an actual BusyBox binary against the supplied musl sysroot instead of using a
fake BusyBox replacement.

## Build

```sh
./fetch-build.sh
```

The script downloads BusyBox 1.37.0 from the official BusyBox download site,
applies `busybox.config`, and builds a static x86_64 binary using the Stage-4
musl sysroot. The resulting binary is placed at `out/busybox`.

A successful host build does not yet mean every BusyBox applet will run on
YabroOS-32. Runtime compatibility is handled incrementally after the first real
BusyBox binary is available.
