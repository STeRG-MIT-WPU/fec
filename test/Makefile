# Makefile for verifying fec_sterg inside the Docker container.
#
# Targets:
#   host-test   - build and run the verification harness with system gcc
#   arm-build   - cross-compile all sources with arm-none-eabi-gcc using
#                 Cortex-M4 flags; does not link (no startup/linker script).
#                 Proves the port builds clean for STM32 targets.
#   all         - run both
#   clean       - remove build artifacts
#
# Flat layout — every .c under Src/ gets compiled; every .h under Inc/ is
# discoverable via a single include path.

ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
INC      := $(ROOT)/Inc
# Exclude app_*.c — those need the STM32 HAL and are not built in the
# Docker test harness (they're compiled inside the STM32CubeIDE project).
SRCS     := $(filter-out $(ROOT)/Src/app_%.c,$(wildcard $(ROOT)/Src/*.c))
BUILD    := build

HOST_CC     := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -O2 -I$(INC) -g

ARM_CC     := arm-none-eabi-gcc
ARM_CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Os \
              -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
              -ffunction-sections -fdata-sections \
              -I$(INC)

HOST_OBJS := $(patsubst $(ROOT)/Src/%.c,$(BUILD)/host/%.o,$(SRCS))
ARM_OBJS  := $(patsubst $(ROOT)/Src/%.c,$(BUILD)/arm/%.o,$(SRCS))

.PHONY: all host-test arm-build clean
all: host-test arm-build

# -------- host --------
host-test: $(BUILD)/host/test_runner
	@echo "==> Running host test harness"
	@./$<

$(BUILD)/host/test_runner: $(HOST_OBJS) $(BUILD)/host/test_main.o
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

$(BUILD)/host/test_main.o: $(ROOT)/test/test_main.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

$(BUILD)/host/%.o: $(ROOT)/Src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# -------- arm cross-compile --------
arm-build: $(ARM_OBJS)
	@echo "==> Cross-compile OK: $(words $(ARM_OBJS)) Cortex-M4 objects built"
	@arm-none-eabi-size $(ARM_OBJS) | tail -n +1

$(BUILD)/arm/%.o: $(ROOT)/Src/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
