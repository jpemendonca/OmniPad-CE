#ifndef BT_RTL_H
#define BT_RTL_H

#include <stdint.h>
#include <stddef.h>

/* Realtek USB Bluetooth chips (RTL8761B and friends) boot from ROM and need a firmware patch
 * before they can page or inquire reliably. The patch comes from linux-firmware (rtl_bt/) in the
 * original "Realtech" format and is sent with vendor command 0xFC20, as Linux btrtl does. */

#define RTL_FRAGMENT_LEN 252

typedef struct {
    uint32_t fw_version;
    int      patch_index;   /* which patch in the file matches the chip's ROM */
    int      patch_len;
    int      config_len;
    int      project_id;    /* -1 when the file carries none */
} rtl_fw_info_t;

/* Chip-side project id the patch must have, from the controller's LMP subversion and HCI
 * revision (Read_Local_Version); -1 for chips this build has no firmware for. */
int rtl_expected_project(uint16_t lmp_subversion, uint16_t hci_revision);

/* Builds the byte stream to download (patch + config) for a chip with this ROM version.
 * Returns its length, or a negative error: -1 bad file, -2 no patch for this ROM, -3 out too small. */
int rtl_fw_build(const uint8_t *fw, size_t fw_len, const uint8_t *config, size_t config_len,
                 uint8_t rom_version, uint8_t *out, size_t out_max, rtl_fw_info_t *info);

/* Fragment index byte for fragment n of total (the last one has bit 7 set) */
uint8_t rtl_fragment_index(int n, int total);

#endif /* BT_RTL_H */
