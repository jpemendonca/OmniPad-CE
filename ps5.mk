# OmniPad CE — PS5 Payload Makefile
# Compatible with PS5 Firmware 7.00 through 13.60 (Relapse Exploit)

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION   := $(shell sed -E -n 's/.*ANYPAD_VERSION[[:space:]]+"([^"]+)".*/\1/p' src/version.h | tr -d '\r')
BUILD_NUM := $(shell sed -E -n 's/.*ANYPAD_BUILD[[:space:]]+([0-9]+).*/\1/p' src/version.h | tr -d '\r')
ELF       := dist/OmniPad-CE-$(VERSION)-b$(BUILD_NUM).elf
BUILD     := build/ps5
GEN       := $(BUILD)/gen

CFLAGS    := -std=c11 -Wall -Wextra -O2 -Isrc -I$(GEN)
CFLAGS    += -DANYPAD_VERSION='"$(VERSION)"' -DANYPAD_BUILD=$(BUILD_NUM)
LDLIBS    += -lScePad -lSceUserService -lSceSystemService -lSceAppInstUtil -ldl

SRCS := src/util.c src/log.c src/profiles.c \
        src/usb_controllers.c src/pad_drivers.c src/usb_hotplug.c \
        src/bt_rtl.c src/bt_adapter.c src/launcher.c \
        src/bt/host.c src/bt/le.c src/bt/smp_crypto.c src/bt/bt_profiles.c src/bt/generic.c src/bt/crc32.c \
        src/shellui_inject.c src/ps5_vpad.c \
        src/web.c src/tcp_stream.c src/main.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@cp $@ dist/OmniPad-CE-$(VERSION).elf
	@cp $@ dist/OmniPad-CE.elf
	@echo "[+] Built successfully: $@"
	@echo "[+] Canonical artifact: dist/OmniPad-CE.elf"

HEADERS := $(wildcard src/*.h src/bt/*.h)

$(BUILD)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Dashboard page, embedded into the payload as a C array
$(GEN)/web_index.h: web/index.html tools/embed_web.py
	@mkdir -p $(GEN)
	python3 tools/embed_web.py $< $@ HTML_INDEX

$(BUILD)/web.o: $(GEN)/web_index.h

# The label and number are compiled in: a stamp rebuilds main.o and web.o when they change
$(GEN)/version.stamp: FORCE
	@mkdir -p $(GEN)
	@echo '$(VERSION) $(BUILD_NUM)' | cmp -s - $@ || echo '$(VERSION) $(BUILD_NUM)' > $@
FORCE:
$(BUILD)/main.o $(BUILD)/web.o: $(GEN)/version.stamp

# Realtek Bluetooth firmware from linux-firmware (tools/get-bt-firmware.sh), embedded when present
RTL_DIR := firmware/rtl_bt
$(GEN)/rtl8761bu_fw.h: $(wildcard $(RTL_DIR)/rtl8761bu_fw.bin) tools/embed_web.py
	@mkdir -p $(GEN)
	python3 tools/embed_web.py --optional $(RTL_DIR)/rtl8761bu_fw.bin $@ RTL8761BU_FW

$(GEN)/rtl8761bu_config.h: $(wildcard $(RTL_DIR)/rtl8761bu_config.bin) tools/embed_web.py
	@mkdir -p $(GEN)
	python3 tools/embed_web.py --optional $(RTL_DIR)/rtl8761bu_config.bin $@ RTL8761BU_CONFIG

$(BUILD)/bt_adapter.o: $(GEN)/rtl8761bu_fw.h $(GEN)/rtl8761bu_config.h

# Icon of the Media tab tile (assets/launcher/icon.html renders it)
$(GEN)/launcher_icon.h: assets/launcher/icon0.png tools/embed_web.py
	@mkdir -p $(GEN)
	python3 tools/embed_web.py $< $@ LAUNCHER_ICON

$(BUILD)/launcher.o: $(GEN)/launcher_icon.h

clean:
	rm -rf $(BUILD)
