#include "bt_rtl.h"

#include <string.h>

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int rtl_expected_project(uint16_t lmp_subversion, uint16_t hci_revision)
{
    /* Same ids as Linux btrtl's project_id_to_lmp_subver table */
    if (lmp_subversion == 0x8761 && hci_revision == 0x000b) return 14; /* RTL8761B(U) */
    if (lmp_subversion == 0x8761 && hci_revision == 0x000a) return 3;  /* RTL8761A */
    return -1;
}

/* The project id sits in an extension section read backwards from the end of the file */
static int rtl_project_id(const uint8_t *fw, size_t fw_len)
{
    static const uint8_t sig[4] = { 0x51, 0x04, 0xfd, 0x77 };
    if (fw_len < 14 + 3 + sizeof(sig)) return -1;
    const uint8_t *p = fw + fw_len - sizeof(sig);
    if (memcmp(p, sig, sizeof(sig)) != 0) return -1;
    while (p >= fw + 14 + 3) {
        uint8_t opcode = *--p;
        uint8_t length = *--p;
        uint8_t data = *--p;
        if (opcode == 0xff) break;
        if (length == 0) return -1;
        if (opcode == 0 && length == 1) return data;
        if ((size_t)(p - fw) < length) return -1;
        p -= length;
    }
    return -1;
}

int rtl_fw_build(const uint8_t *fw, size_t fw_len, const uint8_t *config, size_t config_len,
                 uint8_t rom_version, uint8_t *out, size_t out_max, rtl_fw_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->patch_index = -1;
    if (!fw || fw_len < 14 || memcmp(fw, "Realtech", 8) != 0) return -1;

    info->fw_version = get32(fw + 8);
    int num = get16(fw + 12);
    if (num <= 0 || 14 + (size_t)num * 8 > fw_len) return -1;
    info->project_id = rtl_project_id(fw, fw_len);

    const uint8_t *chip_ids = fw + 14;
    const uint8_t *lengths = chip_ids + 2 * num;
    const uint8_t *offsets = chip_ids + 4 * num;
    for (int i = 0; i < num; i++) {
        if (get16(chip_ids + 2 * i) != rom_version + 1) continue;
        uint32_t len = get16(lengths + 2 * i);
        uint32_t off = get32(offsets + 4 * i);
        if (len < 4 || off > fw_len || len > fw_len - off) return -1;
        if (len + config_len > out_max) return -3;
        memcpy(out, fw + off, len);
        /* The patch's last word carries the firmware version */
        memcpy(out + len - 4, fw + 8, 4);
        if (config && config_len) memcpy(out + len, config, config_len);
        info->patch_index = i;
        info->patch_len = (int)len;
        info->config_len = (int)config_len;
        return (int)(len + config_len);
    }
    return -2;
}

uint8_t rtl_fragment_index(int n, int total)
{
    uint8_t index = (uint8_t)(n <= 0x7f ? n : (n & 0x7f) + 1);
    if (n == total - 1) index |= 0x80;
    return index;
}
