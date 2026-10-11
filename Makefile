# Firmware build with arm-none-eabi-gcc (Ubuntu Linux / WSL), without IAR.
#   sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi make
#   make            firmware -> build/Termostat.elf, .hex, .bin
#   make test       host unit tests (tests/Makefile)
#   make clean

TARGET    := Termostat
BUILD_DIR ?= build

PREFIX    ?= arm-none-eabi-
CC        := $(PREFIX)gcc
OBJCOPY   := $(PREFIX)objcopy
SIZE      := $(PREFIX)size

LDSCRIPT  := gcc/$(TARGET).ld

SRCS := src/main.c \
        src/app/settings.c src/app/thermostat.c src/app/ui.c \
        src/drivers/board.c src/drivers/ds18b20.c src/drivers/flash.c src/drivers/HD44780.c \
        src/lib/crc.c src/lib/menu_system.c src/lib/regulator.c \
        src/system/system_stm32f0xx.c \
        gcc/startup_gcc.c

DEFS := STM32F030 STM32F030x6
INCS := src src/app src/lib src/drivers src/system src/cmsis

OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))

ARCH    := -mcpu=cortex-m0 -mthumb -mfloat-abi=soft
CFLAGS  := $(ARCH) -std=gnu11 -Os -g -Wall -Wextra \
           -ffunction-sections -fdata-sections -fno-common \
           $(addprefix -D,$(DEFS)) $(addprefix -I,$(INCS)) \
           -MMD -MP
LDFLAGS := $(ARCH) -nostartfiles --specs=nano.specs --specs=nosys.specs \
           -T$(LDSCRIPT) \
           -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
           -Wl,--print-memory-usage

.PHONY: all clean test

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

test:
	$(MAKE) -C tests

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C tests clean

-include $(OBJS:.o=.d)
