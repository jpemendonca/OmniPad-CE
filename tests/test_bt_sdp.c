/* The host's SDP server (src/bt/host.c): a DualShock 4 calling back opens SDP on us first and
 * hangs up when that is refused. host.c is included so its static handlers can be driven. */
#include "../src/bt/host.c"

#include <stdio.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static unsigned char sent[8][HCI_PKT_MAX];
static int sent_len[8], nsent;

static int fake_acl(void *ctx, const unsigned char *pkt, int len)
{
    (void)ctx;
    if (nsent < 8) { memcpy(sent[nsent], pkt, (size_t)len); sent_len[nsent] = len; }
    nsent++;
    return 1;
}
static int fake_cmd(void *ctx, unsigned op, const void *p, int n) { (void)ctx; (void)op; (void)p; (void)n; return 1; }

static const hci_ops k_fake = { .cmd = fake_cmd, .acl_send = fake_acl };

static host_t g_h;

static link_t *fresh_link(void)
{
    memset(&g_h, 0, sizeof g_h);
    g_h.hci.ops = &k_fake;
    g_h.acl_mtu = 1021;
    g_h.br.credits = g_h.br.max = 6;
    static const unsigned char addr[6] = { 0x1b, 0x10, 0x93, 0x5f, 0x89, 0x90 };
    link_t *l = link_new(&g_h, addr);
    l->connected = 1;
    l->handle = 1;
    nsent = 0;
    return l;
}

/* The payload of the last frame we sent (after the ACL and L2CAP headers) */
static const unsigned char *last_payload(int *len, unsigned *cid)
{
    const unsigned char *p = sent[(nsent - 1) % 8];
    *len = (int)le16(p + 4);
    *cid = le16(p + 6);
    return p + 8;
}

static void test_channel_accepted(void)
{
    link_t *l = fresh_link();
    /* What the DS4 sent on the PS5: Connection Request, PSM 0x0001, source CID 0x0040 */
    static const unsigned char req[] = { 0x02, 0x01, 0x04, 0x00, 0x01, 0x00, 0x40, 0x00 };
    on_frame(&g_h, l, CID_SIGNALING, req, sizeof req);
    CHECK(nsent == 2, "connection response and our configuration request, got %d frames", nsent);
    const unsigned char *rsp = sent[0] + 8;
    CHECK(rsp[0] == SIG_CONN_RSP && le16(rsp + 4) == CID_SDP_SRV && le16(rsp + 6) == 0x0040,
          "response carries our CID 0x43 for their 0x40");
    CHECK(le16(rsp + 8) == 0, "result success, not 'PSM not supported' (got %u)", le16(rsp + 8));
    CHECK(l->sdps.st == CH_CONFIG && l->sdps.dcid == 0x0040, "server channel configuring");
}

static link_t *open_server(void)
{
    link_t *l = fresh_link();
    l->sdps.st = CH_OPEN;
    l->sdps.dcid = 0x0040;
    return l;
}

static void test_search_attribute(void)
{
    link_t *l = open_server();
    /* Service Search Attribute: UUID 0x1200, max 0x40 bytes, attributes 0x0000-0xffff */
    static const unsigned char req[] = { 0x06, 0x00, 0x07, 0x00, 0x0f,
        0x35, 0x03, 0x19, 0x12, 0x00, 0x00, 0x40, 0x35, 0x05, 0x0a, 0x00, 0x00, 0xff, 0xff, 0x00 };
    on_frame(&g_h, l, CID_SDP_SRV, req, sizeof req);
    int len; unsigned cid;
    const unsigned char *r = last_payload(&len, &cid);
    CHECK(cid == 0x0040, "answer goes to the pad's CID");
    CHECK(r[0] == 0x07 && be16(r + 1) == 0x0007, "Search Attribute response, same transaction");
    CHECK((int)be16(r + 3) == len - 5, "parameter length matches");
    int lists = (int)be16(r + 5);
    CHECK(r[7] == 0x35 && r[8] == lists - 2 && r[9] == 0x35 && r[10] == lists - 4, "one record in a sequence");
    CHECK(r[11 + r[10]] == 0x00, "no continuation");
    uint16_t vid = 0, pid = 0;
    CHECK(sdp_find_ids(r + 7, lists, &vid, &pid) && vid == 0x1d6b && pid == 0x0246,
          "Device ID readable by our own client (%04x:%04x)", vid, pid);
}

static void test_no_match(void)
{
    link_t *l = open_server();
    /* A search for HID (0x1124), which we do not serve */
    static const unsigned char req[] = { 0x06, 0x00, 0x02, 0x00, 0x0f,
        0x35, 0x03, 0x19, 0x11, 0x24, 0x00, 0x40, 0x35, 0x05, 0x0a, 0x00, 0x00, 0xff, 0xff, 0x00 };
    on_frame(&g_h, l, CID_SDP_SRV, req, sizeof req);
    int len; unsigned cid;
    const unsigned char *r = last_payload(&len, &cid);
    CHECK(r[0] == 0x07 && be16(r + 5) == 2 && r[7] == 0x35 && r[8] == 0 && r[9] == 0, "empty list: 35 00, no continuation");
}

static void test_search_and_attribute(void)
{
    link_t *l = open_server();
    static const unsigned char search[] = { 0x02, 0x00, 0x03, 0x00, 0x08,
        0x35, 0x03, 0x19, 0x10, 0x02, 0x00, 0x05, 0x00 };
    on_frame(&g_h, l, CID_SDP_SRV, search, sizeof search);
    int len; unsigned cid;
    const unsigned char *r = last_payload(&len, &cid);
    CHECK(r[0] == 0x03 && be16(r + 5) == 1 && be16(r + 7) == 1 && be32(r + 9) == SDP_SRV_HANDLE,
          "browse search finds our one record");

    /* Service Attribute for the vendor id only */
    static const unsigned char attr[] = { 0x04, 0x00, 0x04, 0x00, 0x0c,
        0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x35, 0x03, 0x09, 0x02, 0x01, 0x00 };
    on_frame(&g_h, l, CID_SDP_SRV, attr, sizeof attr);
    r = last_payload(&len, &cid);
    static const unsigned char want[] = { 0x35, 0x06, 0x09, 0x02, 0x01, 0x09, 0x1d, 0x6b };
    CHECK(r[0] == 0x05 && be16(r + 5) == sizeof want && memcmp(r + 7, want, sizeof want) == 0,
          "only the vendor attribute comes back");

    static const unsigned char bad[] = { 0x04, 0x00, 0x05, 0x00, 0x0c,
        0x00, 0x09, 0x00, 0x09, 0x00, 0x40, 0x35, 0x03, 0x09, 0x02, 0x01, 0x00 };
    on_frame(&g_h, l, CID_SDP_SRV, bad, sizeof bad);
    r = last_payload(&len, &cid);
    CHECK(r[0] == 0x01 && be16(r + 5) == 0x0002, "unknown handle: error 0x0002");
}

static void test_server_close_keeps_link(void)
{
    link_t *l = open_server();
    l->ready = 1;
    /* The pad closes its SDP channel: Disconnection Request, dcid 0x43 (ours), scid 0x40 */
    static const unsigned char req[] = { 0x06, 0x02, 0x04, 0x00, 0x43, 0x00, 0x40, 0x00 };
    on_frame(&g_h, l, CID_SIGNALING, req, sizeof req);
    CHECK(!l->disconnecting, "closing SDP does not drop the pad");
    CHECK(l->sdps.st == CH_CLOSED, "server channel closed");
}

int main(void)
{
    test_channel_accepted();
    test_search_attribute();
    test_no_match();
    test_search_and_attribute();
    test_server_close_keeps_link();
    printf("test_bt_sdp: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
