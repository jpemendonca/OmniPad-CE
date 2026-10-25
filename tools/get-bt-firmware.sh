#!/bin/sh
# Downloads the Realtek Bluetooth firmware OmniPad embeds (from linux-firmware) into firmware/rtl_bt.
# Redistributable binary under linux-firmware's LICENCE.rtlwifi_firmware.txt; not kept in this repo.
set -e
cd "$(dirname "$0")/.."
mkdir -p firmware/rtl_bt
base=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain
for f in rtl_bt/rtl8761bu_fw.bin rtl_bt/rtl8761bu_config.bin LICENSES/LICENCE.rtlwifi_firmware.txt; do
    curl -sSfL -o "firmware/rtl_bt/$(basename "$f")" "$base/$f"
    echo "$(basename "$f"): $(wc -c < "firmware/rtl_bt/$(basename "$f")") bytes"
done
