#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

long now_ms(void);
uint64_t uptime_ms(void);
void sleep_ms(int ms);
uint32_t crc32_le(uint32_t crc, const uint8_t *buf, size_t len);
uint64_t hex_to_u64(const char *s);
int hex_dump(const void *data, size_t len, char *out, size_t out_len);

/* Byte order for the Bluetooth stack (src/bt): HCI, L2CAP and HID are little-endian, SDP big-endian */
static inline unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static inline unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static inline uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline int16_t les16(const unsigned char *p) { return (int16_t)le16(p); }
static inline void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}
static inline void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

#endif /* UTIL_H */
