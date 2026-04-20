# Makefile for verifying fec_sterg inside the Docker container.
#
# Targets:
#   host-test   - build and run the verification harness with system gcc
#   arm-build   - cross-compile all sources with arm-none-eabi-gcc using
#                 Cortex-M4 flags; does not link (no startup/linker script).
#                 Proves the port builds clean for STM32 targets.
#   all         - run both
#   clean       - remove build artifacts

ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
INC      := $(ROOT)/Inc
SRC_DIRS := $(ROOT)/Src/convolutional $(ROOT)/Src/reed-solomon
SRCS     := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
BUILD    := build

HOST_CC     := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -O2 -I$(INC) -g

ARM_CC     := arm-none-eabi-gcc
ARM_CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Os \
              -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
              -ffunction-sections -fdata-sections \
              -I$(INC)

# Give each object a path-prefixed name so bit.c and the two encode.c/decode.c
# do not collide.
host_obj = $(BUILD)/host/$(subst /,__,$(patsubst $(ROOT)/Src/%,%,$(1:.c=.o)))
arm_obj  = $(BUILD)/arm/$(subst /,__,$(patsubst $(ROOT)/Src/%,%,$(1:.c=.o)))

HOST_OBJS := $(foreach s,$(SRCS),$(call host_obj,$(s)))
ARM_OBJS  := $(foreach s,$(SRCS),$(call arm_obj,$(s)))

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

# one pattern per source directory because the prefix differs
$(BUILD)/host/convolutional__%.o: $(ROOT)/Src/convolutional/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

$(BUILD)/host/reed-solomon__%.o: $(ROOT)/Src/reed-solomon/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# -------- arm cross-compile --------
arm-build: $(ARM_OBJS)
	@echo "==> Cross-compile OK: $(words $(ARM_OBJS)) Cortex-M4 objects built"
	@arm-none-eabi-size $(ARM_OBJS) | tail -n +1

$(BUILD)/arm/convolutional__%.o: $(ROOT)/Src/convolutional/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

$(BUILD)/arm/reed-solomon__%.o: $(ROOT)/Src/reed-solomon/%.c
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
