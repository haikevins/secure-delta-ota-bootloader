# Common STM32F103C8T6 build rules shared by bootloader and application.
# The build prefers GNU Arm Embedded GCC and falls back to Clang/LLD when GCC
# is not installed. Override with TOOLCHAIN=gcc or TOOLCHAIN=clang.

ifndef TARGET
$(error TARGET must be defined before including mk/stm32f103.mk)
endif
ifndef LINKER_SCRIPT
$(error LINKER_SCRIPT must be defined before including mk/stm32f103.mk)
endif
ifndef ROOT_DIR
$(error ROOT_DIR must be defined before including mk/stm32f103.mk)
endif

NODE_DIR       := $(ROOT_DIR)/node-stm32f103
SPL_DIR        := $(NODE_DIR)/spl
CMSIS_DIR      := $(NODE_DIR)/cmsis
CMSIS_CORE_DIR := $(CMSIS_DIR)/CM3/CoreSupport
CMSIS_DEV_DIR  := $(CMSIS_DIR)/CM3/DeviceSupport/ST/STM32F10x
CONFIG_DIR     := $(NODE_DIR)/config
SHARED_DIR     := $(ROOT_DIR)/shared

BUILD_DIR ?= build
OUT_DIR   ?= out

ifeq ($(origin TOOLCHAIN), undefined)
  ifneq ($(shell command -v arm-none-eabi-gcc 2>/dev/null),)
    TOOLCHAIN := gcc
  else ifneq ($(shell command -v clang 2>/dev/null),)
    TOOLCHAIN := clang
  else
    $(error No supported ARM toolchain found. Install arm-none-eabi-gcc or clang/lld)
  endif
endif

ifeq ($(TOOLCHAIN),gcc)
  CC       := arm-none-eabi-gcc
  OBJCOPY  := arm-none-eabi-objcopy
  SIZE     := arm-none-eabi-size
  TOOLCHAIN_COMPILE_FLAGS :=
  TOOLCHAIN_WARNING_FLAGS :=
  TOOLCHAIN_LINK_FLAGS := -nostdlib
  TOOLCHAIN_LIBS       := -lgcc
else ifeq ($(TOOLCHAIN),clang)
  CC       := clang
  OBJCOPY  := llvm-objcopy
  SIZE     := size
  TOOLCHAIN_COMPILE_FLAGS := --target=arm-none-eabi
  TOOLCHAIN_WARNING_FLAGS := -Wno-invalid-utf8 -Wno-deprecated-non-prototype -Wno-strict-prototypes
  TOOLCHAIN_LINK_FLAGS := --target=arm-none-eabi -fuse-ld=lld -nostdlib
  TOOLCHAIN_LIBS       :=
else
  $(error Unsupported TOOLCHAIN='$(TOOLCHAIN)'; choose gcc or clang)
endif

ARCH_FLAGS := -mcpu=cortex-m3 -mthumb
COMMON_DEFINES := \
  -DSTM32F10X_MD \
  -DUSE_STDPERIPH_DRIVER \
  -DHSE_VALUE=8000000UL

INCLUDE_DIRS := \
  $(CONFIG_DIR) \
  $(SPL_DIR)/inc \
  $(CMSIS_CORE_DIR) \
  $(CMSIS_DEV_DIR) \
  $(SHARED_DIR)/include \
  include \
  crypto \
  patch

CPPFLAGS := $(COMMON_DEFINES) $(PROJECT_DEFINES) $(addprefix -I,$(INCLUDE_DIRS))
CFLAGS := $(TOOLCHAIN_COMPILE_FLAGS) $(ARCH_FLAGS) \
  -std=c11 \
  -Os \
  -g3 \
  -ffreestanding \
  -fno-builtin \
  -fno-common \
  -fno-stack-protector \
  -fno-unwind-tables \
  -fno-asynchronous-unwind-tables \
  -ffunction-sections \
  -fdata-sections \
  -Wall \
  -Wextra \
  -Wpedantic \
  -Wshadow \
  -Wundef \
  -Wdouble-promotion \
  $(TOOLCHAIN_WARNING_FLAGS) \
  $(PROJECT_CFLAGS)
ASFLAGS := $(TOOLCHAIN_COMPILE_FLAGS) $(ARCH_FLAGS) -g3

LDFLAGS := $(ARCH_FLAGS) $(TOOLCHAIN_LINK_FLAGS) \
  -Wl,-T,$(LINKER_SCRIPT) \
  -Wl,-Map,$(OUT_DIR)/$(TARGET).map \
  -Wl,--gc-sections \
  -Wl,--build-id=none \
  -Wl,--entry=Reset_Handler

# Minimal SPL subset used by the Phase 1 heartbeat. Later phases add their
# required modules explicitly instead of linking the whole vendor library.
SPL_C_SOURCES := \
  $(SPL_DIR)/src/misc.c \
  $(SPL_DIR)/src/stm32f10x_gpio.c \
  $(SPL_DIR)/src/stm32f10x_rcc.c

C_SOURCES := $(LOCAL_C_SOURCES) $(SPL_C_SOURCES)
ASM_SOURCES := $(LOCAL_ASM_SOURCES)

# Keep object names flat. The selected source set intentionally has unique
# basenames; phase1_check.py verifies this invariant.
C_OBJECTS := $(foreach source,$(C_SOURCES),$(BUILD_DIR)/$(notdir $(source:.c=.o)))
ASM_OBJECTS := $(foreach source,$(ASM_SOURCES),$(BUILD_DIR)/$(notdir $(source:.s=.o)))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
DEPS := $(C_OBJECTS:.o=.d)

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

ELF := $(OUT_DIR)/$(TARGET).elf
BIN := $(OUT_DIR)/$(TARGET).bin
HEX := $(OUT_DIR)/$(TARGET).hex

.PHONY: all clean info size list-sources

all: $(ELF) $(BIN) $(HEX) size

$(ELF): $(OBJECTS) $(LINKER_SCRIPT) | $(OUT_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(TOOLCHAIN_LIBS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR) $(OUT_DIR):
	mkdir -p $@

size: $(ELF)
	@$(SIZE) $< | tee $(OUT_DIR)/$(TARGET).size.txt

info:
	@echo "Target      : $(TARGET)"
	@echo "Toolchain   : $(TOOLCHAIN)"
	@echo "Compiler    : $(CC)"
	@echo "Linker file : $(LINKER_SCRIPT)"
	@echo "ELF         : $(ELF)"

list-sources:
	@printf '%s\n' $(C_SOURCES) $(ASM_SOURCES)

clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

-include $(DEPS)
