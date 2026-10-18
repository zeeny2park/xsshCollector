#
# Glances Embedded Linux Collector Makefile
#
# SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
# SPDX-License-Identifier: LGPL-3.0-only
#

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -Werror -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -O2
INCLUDES = -Iinclude
LDFLAGS ?=

BUILD_DIR = build
SRC_DIR = src

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/json_writer.c \
       $(SRC_DIR)/sysinfo.c \
       $(SRC_DIR)/capabilities.c \
       $(SRC_DIR)/summary.c \
       $(SRC_DIR)/process.c \
       $(SRC_DIR)/exec.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET = $(BUILD_DIR)/glances-collector
TARGET_STATIC = $(BUILD_DIR)/glances-collector-static

.PHONY: all clean static cross-arm cross-aarch64 probe test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built: $@"
	@file $@

static: $(OBJS)
	@echo "Attempting static build with $(CC)..."
	@if $(CC) -static $(OBJS) -o $(TARGET_STATIC) $(LDFLAGS) 2>/dev/null; then \
		echo "Static binary successfully built: $(TARGET_STATIC)"; \
		file $(TARGET_STATIC); \
	else \
		echo "ERROR: Static linking failed. Toolchain or static libc (libc.a / musl) not available."; \
		exit 1; \
	fi

cross-aarch64:
	@AARCH64_CC=$$(which aarch64-linux-gnu-gcc 2>/dev/null || which aarch64-none-linux-gnu-gcc 2>/dev/null); \
	if [ -n "$$AARCH64_CC" ]; then \
		echo "Building for AArch64 using $$AARCH64_CC..."; \
		$(MAKE) CC="$$AARCH64_CC" TARGET="$(BUILD_DIR)/glances-collector-aarch64" all; \
	else \
		echo "PROBE RESULT: AArch64 cross-compiler (aarch64-linux-gnu-gcc) not found in PATH."; \
		exit 2; \
	fi

cross-arm:
	@ARM_CC=$$(which arm-linux-gnueabihf-gcc 2>/dev/null || which arm-linux-gnueabi-gcc 2>/dev/null); \
	if [ -n "$$ARM_CC" ]; then \
		echo "Building for ARM using $$ARM_CC..."; \
		$(MAKE) CC="$$ARM_CC" TARGET="$(BUILD_DIR)/glances-collector-arm" all; \
	else \
		echo "PROBE RESULT: ARM cross-compiler (arm-linux-gnueabihf-gcc) not found in PATH."; \
		exit 2; \
	fi

probe:
	@echo "=========================================================="
	@echo " Toolchain and Cross-Compilation Probe Report"
	@echo "=========================================================="
	@echo "Host machine:      $$(uname -m)"
	@echo "Host OS:           $$(uname -s) $$(uname -r)"
	@echo "Default CC:        $$(which $(CC) 2>/dev/null || echo 'not found') ($$($(CC) --version 2>/dev/null | head -n 1))"
	@echo "Clang compiler:    $$(which clang 2>/dev/null || echo 'not found')"
	@echo "AArch64 compiler:  $$(which aarch64-linux-gnu-gcc 2>/dev/null || echo 'not installed')"
	@echo "ARM 32 compiler:   $$(which arm-linux-gnueabihf-gcc 2>/dev/null || echo 'not installed')"
	@echo "Musl-gcc:          $$(which musl-gcc 2>/dev/null || echo 'not installed')"
	@echo -n "Static libc test:  "
	@if echo 'int main(void){return 0;}' | $(CC) -x c -static - -o /tmp/__test_static 2>/dev/null; then \
		echo "Supported (libc.a present)"; \
		rm -f /tmp/__test_static; \
	else \
		echo "Unsupported (libc.a not installed)"; \
	fi
	@echo "=========================================================="

clean:
	rm -rf $(BUILD_DIR)
