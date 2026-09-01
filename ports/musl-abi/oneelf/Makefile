MUSL_CC ?= musl-gcc
CFLAGS ?= -O2 -pipe
LDFLAGS ?= -static
OUT ?= ../../../build/musl-abi
NAME ?= yabro-musl-tests.elf

SOURCES := runner.c 01_hello.c 02_memory.c 03_thread.c 04_fs.c 05_time_random.c 06_fds.c 07_socketpair.c 08_tls_pthread.c

.PHONY: all clean install

all: $(OUT)/$(NAME)

$(OUT):
	mkdir -p $@

$(OUT)/$(NAME): $(SOURCES) | $(OUT)
	$(MUSL_CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

install: all
	@echo "ELF: $(OUT)/$(NAME)"

clean:
	rm -rf $(OUT)
