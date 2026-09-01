MUSL_CC ?= musl-gcc
CFLAGS ?= -O2 -pipe
LDFLAGS_STATIC ?= -static
LDFLAGS_PIE ?= -static-pie -fPIE -pie
OUT := out
SRCS := $(wildcard tests/*.c)
BINS := $(patsubst tests/%.c,$(OUT)/%,$(SRCS))

.PHONY: all static-pie clean
all: $(BINS)

$(OUT):
	mkdir -p $@

$(OUT)/%: tests/%.c | $(OUT)
	$(MUSL_CC) $(CFLAGS) $< $(LDFLAGS_STATIC) -o $@

static-pie: | $(OUT)
	$(MUSL_CC) $(CFLAGS) tests/01_hello.c $(LDFLAGS_PIE) -o $(OUT)/01_hello-pie

clean:
	rm -rf $(OUT)
