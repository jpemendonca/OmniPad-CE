/* Realtek firmware patch selection (src/bt_rtl.c), the same steps that loaded the RTL8761BU on the PC */
#include "bt_rtl.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

/* Two patches (chip ids 1 and 2), then the extension section with project id 14 */
static size_t make_fw(uint8_t *fw)
{
    memset(fw, 0, 256);
    memcpy(fw, "Realtech", 8);
    put32(fw + 8, 0x11223344);
    put16(fw + 12, 2);
    put16(fw + 14, 1); put16(fw + 16, 2);          /* chip ids */
    put16(fw + 18, 8); put16(fw + 20, 12);         /* lengths */
    put32(fw + 22, 30); put32(fw + 26, 38);        /* offsets */
    for (int i = 0; i < 8; i++) fw[30 + i] = (uint8_t)(0xA0 + i);
    for (int i = 0; i < 12; i++) fw[38 + i] = (uint8_t)(0xB0 + i);
    size_t n = 50;
    fw[n++] = 14; fw[n++] = 1; fw[n++] = 0;        /* data, length, opcode (read backwards) */
    fw[n++] = 0x51; fw[n++] = 0x04; fw[n++] = 0xfd; fw[n++] = 0x77;
    return n;
}

static void test_synthetic(void)
{
    uint8_t fw[256], out[256];
    const uint8_t cfg[3] = { 0x55, 0xab, 0x23 };
    size_t len = make_fw(fw);
    rtl_fw_info_t info;

    int n = rtl_fw_build(fw, len, cfg, sizeof(cfg), 1, out, sizeof(out), &info);
    CHECK(n == 12 + 3, "ROM 1 picks the 12-byte patch plus config, got %d", n);
    CHECK(info.patch_index == 1 && info.project_id == 14, "patch %d project %d", info.patch_index, info.project_id);
    CHECK(out[0] == 0xB0 && out[7] == 0xB7, "patch bytes copied");
    CHECK(out[8] == 0x44 && out[11] == 0x11, "last word replaced by the firmware version");
    CHECK(out[12] == 0x55 && out[14] == 0x23, "config appended");

    n = rtl_fw_build(fw, len, NULL, 0, 0, out, sizeof(out), &info);
    CHECK(n == 8 && info.patch_index == 0, "ROM 0 picks the first patch, got %d", n);
    CHECK(rtl_fw_build(fw, len, NULL, 0, 5, out, sizeof(out), &info) == -2, "no patch for ROM 5");
    CHECK(rtl_fw_build(fw, len, NULL, 0, 1, out, 10, &info) == -3, "output too small");

    fw[0] = 'X';
    CHECK(rtl_fw_build(fw, len, NULL, 0, 1, out, sizeof(out), &info) == -1, "bad signature");
    fw[0] = 'R';
    put16(fw + 20, 200);
    CHECK(rtl_fw_build(fw, len, NULL, 0, 1, out, sizeof(out), &info) == -1, "patch past end of file");
}

static void test_ids(void)
{
    CHECK(rtl_expected_project(0x8761, 0x000b) == 14, "RTL8761B");
    CHECK(rtl_expected_project(0x8761, 0x000a) == 3, "RTL8761A");
    CHECK(rtl_expected_project(0x8822, 0x000b) == -1, "unknown chip");
    CHECK(rtl_fragment_index(0, 3) == 0x00, "first fragment");
    CHECK(rtl_fragment_index(2, 3) == 0x82, "last fragment flagged");
    CHECK(rtl_fragment_index(0x80, 200) == 0x01, "index wraps after 0x7f");
}

/* The linux-firmware file, when tools/get-bt-firmware.sh has fetched it */
static void test_real_file(void)
{
    static uint8_t fw[64 * 1024], cfg[64], out[64 * 1024];
    FILE *f = fopen("firmware/rtl_bt/rtl8761bu_fw.bin", "rb");
    if (!f) {
        printf("skip: firmware/rtl_bt/rtl8761bu_fw.bin not present\n");
        return;
    }
    size_t len = fread(fw, 1, sizeof(fw), f);
    fclose(f);
    size_t cfg_len = 0;
    f = fopen("firmware/rtl_bt/rtl8761bu_config.bin", "rb");
    if (f) { cfg_len = fread(cfg, 1, sizeof(cfg), f); fclose(f); }

    int built = 0;
    for (int rom = 0; rom < 8; rom++) {
        rtl_fw_info_t info;
        int n = rtl_fw_build(fw, len, cfg, cfg_len, (uint8_t)rom, out, sizeof(out), &info);
        if (n < 0) continue;
        built++;
        CHECK(info.project_id == 14, "real file is for RTL8761B, project %d", info.project_id);
        CHECK(n == info.patch_len + (int)cfg_len, "length adds up");
        printf("real file: ROM %d -> patch %d, %d bytes, %d fragments\n", rom, info.patch_index, n,
               (n + RTL_FRAGMENT_LEN - 1) / RTL_FRAGMENT_LEN);
    }
    CHECK(built > 0, "real file has a patch for some ROM");
}

int main(void)
{
    test_synthetic();
    test_ids();
    test_real_file();
    printf("test_bt_rtl: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
