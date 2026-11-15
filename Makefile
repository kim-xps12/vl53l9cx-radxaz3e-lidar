# SPDX-License-Identifier: MIT
# VL53L9CX on Radxa ZERO 3E — build everything on the board itself.
APP     := build/vl53l9_bringup
VENDOR  := upstream/vendor/st-uld
CFLAGS  += -O2 -Wall -I$(VENDOR)
LDLIBS  += -lrt -lpthread -lm
SRC     := upstream/app/vl53l9_bringup.c \
           $(VENDOR)/vl53l9.c \
           $(VENDOR)/vl53l9_platform.c \
           $(VENDOR)/vl53l9_utils.c
DTBO    := build/radxa-zero3-vl53l9cx.dtbo

.PHONY: all app module dtbo clean
all: app module dtbo

app: $(APP)
$(APP): $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

module:
	$(MAKE) -C radxa/driver

dtbo: $(DTBO)
$(DTBO): radxa/dts/radxa-zero3-vl53l9cx.dts
	@mkdir -p build
	dtc -@ -q -I dts -O dtb -o $@ $<

clean:
	rm -rf build
	-$(MAKE) -C radxa/driver clean
