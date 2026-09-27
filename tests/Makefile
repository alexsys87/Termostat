# Host unit tests for the hardware independent modules.
# Usage: make -C tests        (requires a host C compiler)

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O1 -g
INC      = -I. -Istubs -I../src/lib -I../src/app -I../src/drivers
BUILD    = build

TESTS    = test_crc test_regulator test_menu test_settings

all: run

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_crc: test_crc.c ../src/lib/crc.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

$(BUILD)/test_regulator: test_regulator.c ../src/lib/regulator.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm

$(BUILD)/test_menu: test_menu.c ../src/lib/menu_system.c ../src/lib/regulator.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

$(BUILD)/test_settings: test_settings.c ../src/app/settings.c ../src/lib/crc.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -include fake_flash.h -DSETTINGS_FLASH_ADDRESS='((uintptr_t)test_flash)' -o $@ $^

run: $(addprefix $(BUILD)/,$(TESTS))
	@fail=0; for t in $^; do ./$$t || fail=1; done; exit $$fail

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
