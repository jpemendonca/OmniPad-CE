# OmniPad PS5
# Universal Controller Engine & Web Control Center for PS5 (FW 7.00 - 13.60)
# Combines Bluetooth (AnyPad-PS5) + USB Hotplug (Ghostcontrol / PoorDS4) + ShellUI Fixes (YetAnotherControllerEnabler)

PS5_HOST ?= 192.168.1.22
PS5_PORT ?= 9021

BUILD := build

.PHONY: all ps5 send clean test host-test

all: ps5

ps5:
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined. Please export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk)
endif
	$(MAKE) -f ps5.mk

send: ps5
	@echo "[*] Sending payload to PS5 $(PS5_HOST):$(PS5_PORT)..."
	nc -w 5 $(PS5_HOST) $(PS5_PORT) < dist/OmniPad-PS5-*.elf || \
	python3 -c "import socket, glob; f=open(glob.glob('dist/OmniPad-PS5-*.elf')[0],'rb').read(); s=socket.create_connection(('$(PS5_HOST)', $(PS5_PORT)), timeout=5); s.sendall(f); s.close(); print('[+] Envio concluido!')"

test: host-test

HOST_SRCS := src/pad_drivers.c src/usb_controllers.c src/profiles.c src/util.c src/log.c

host-test:
	@mkdir -p $(BUILD)
	clang -O2 -Isrc $(HOST_SRCS) tests/test_drivers.c -o $(BUILD)/test_drivers
	./$(BUILD)/test_drivers
	clang -O2 -Isrc $(HOST_SRCS) tests/test_parsers.c -o $(BUILD)/test_parsers
	./$(BUILD)/test_parsers
	clang -O2 -Isrc $(HOST_SRCS) tests/test_flow_sim.c -o $(BUILD)/test_flow_sim
	./$(BUILD)/test_flow_sim
	clang -O2 -Isrc tests/test_vpad_abi.c -o $(BUILD)/test_vpad_abi
	./$(BUILD)/test_vpad_abi
	clang -O2 -Isrc tests/test_pad_struct.c -o $(BUILD)/test_pad_struct
	./$(BUILD)/test_pad_struct
	clang -O2 -Isrc src/bt_rtl.c tests/test_bt_rtl.c -o $(BUILD)/test_bt_rtl
	./$(BUILD)/test_bt_rtl
	clang -O1 -Isrc -Isrc/bt -Wno-unused-function tests/test_bt_sdp.c src/bt/le.c src/bt/smp_crypto.c src/bt/bt_profiles.c src/bt/generic.c src/bt/crc32.c src/util.c src/log.c -o $(BUILD)/test_bt_sdp
	./$(BUILD)/test_bt_sdp

clean:
	rm -rf $(BUILD)
