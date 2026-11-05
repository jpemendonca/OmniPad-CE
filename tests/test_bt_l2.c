/* How l2_send (src/bt/host.c) hands frames to the controller. host.c is included so a link can be
 * built by hand. An Xbox pad never paired on an adapter with a 27-byte LE buffer (issue 4). */
#include "../src/bt/host.c"

#include <stdio.h>

#define CID_SMP 0x0006

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static unsigned char sent[8][HCI_PKT_MAX];
static int sent_len[8], nsent, acl_fails_at = -1;

static int fake_acl(void *ctx, const unsigned char *pkt, int len)
{
    (void)ctx;
    if (nsent == acl_fails_at) return 0;
    if (nsent < 8) { memcpy(sent[nsent], pkt, (size_t)len); sent_len[nsent] = len; }
    nsent++;
    return 1;
}
static int fake_cmd(void *ctx, unsigned op, const void *p, int n) { (void)ctx; (void)op; (void)p; (void)n; return 1; }

static const hci_ops k_fake = { .cmd = fake_cmd, .acl_send = fake_acl };

static host_t g_h;

static link_t *le_link_with_buffer(unsigned mtu, int buffers)
{
    memset(&g_h, 0, sizeof g_h);
    g_h.hci.ops = &k_fake;
    g_h.acl_mtu = 1021;
    g_h.br.credits = g_h.br.max = 6;
    g_h.le_separate = 1;
    g_h.le_mtu = mtu;
    g_h.lep.credits = g_h.lep.max = buffers;
    static const unsigned char addr[6] = { 0xf6, 0xea, 0x50, 0x3e, 0x8c, 0xa8 };
    link_t *l = link_new(&g_h, addr);
    l->connected = 1;
    l->le = 1;
    l->handle = 0x010;
    nsent = 0;
    acl_fails_at = -1;
    return l;
}

static void fill(unsigned char *d, int n)
{
    for (int i = 0; i < n; i++) d[i] = (unsigned char)(i + 1);
}

static void test_l2_send_keyOn27ByteBuffer_goesInPieces(void)
{
    /* arrange: the SMP public key, on the buffer the failing adapter is assumed to have */
    link_t *l = le_link_with_buffer(27, 8);
    unsigned char key[65], frame[69];
    fill(key, sizeof key);

    /* act */
    int ok = l2_send(&g_h, l, CID_SMP, key, sizeof key);

    /* assert */
    CHECK(ok && nsent == 3, "69 bytes in 27-byte pieces is 3 packets, got %d", nsent);
    CHECK(le16(sent[0]) == 0x0010, "the first piece is an LE start, got 0x%04x", le16(sent[0]));
    CHECK(le16(sent[1]) == 0x1010 && le16(sent[2]) == 0x1010, "the rest are continuations");
    CHECK(le16(sent[0] + 2) == 27 && le16(sent[1] + 2) == 27 && le16(sent[2] + 2) == 15, "piece sizes");
    memcpy(frame, sent[0] + 4, 27);
    memcpy(frame + 27, sent[1] + 4, 27);
    memcpy(frame + 54, sent[2] + 4, 15);
    CHECK(le16(frame) == 65 && le16(frame + 2) == CID_SMP, "the L2CAP header is at the start");
    CHECK(memcmp(frame + 4, key, sizeof key) == 0, "joined again, it is the same key");
    CHECK(g_h.lep.credits == 5 && g_h.lep.count == 3, "each piece holds a buffer, %d left", g_h.lep.credits);
}

static void test_l2_send_exactFit_staysOnePacket(void)
{
    /* arrange */
    link_t *l = le_link_with_buffer(27, 8);
    unsigned char d[23];
    fill(d, sizeof d);

    /* act */
    int ok = l2_send(&g_h, l, CID_SMP, d, sizeof d);

    /* assert */
    CHECK(ok && nsent == 1 && sent_len[0] == 31, "one whole packet, got %d of %d bytes", nsent, sent_len[0]);
    CHECK(le16(sent[0]) == 0x0010 && le16(sent[0] + 2) == 27 && le16(sent[0] + 4) == 23 && le16(sent[0] + 6) == CID_SMP,
          "the same headers as before the split existed");
    CHECK(memcmp(sent[0] + 8, d, sizeof d) == 0, "the same payload");
    CHECK(g_h.lep.credits == 7, "one buffer taken");
}

static void test_l2_send_keyOnBigBuffer_staysOnePacket(void)
{
    /* arrange: the buffer of the adapters that already paired an Xbox pad */
    link_t *l = le_link_with_buffer(251, 6);
    unsigned char key[65];
    fill(key, sizeof key);

    /* act */
    int ok = l2_send(&g_h, l, CID_SMP, key, sizeof key);

    /* assert */
    CHECK(ok && nsent == 1 && sent_len[0] == 73, "one whole packet, got %d", nsent);
}

static void test_l2_send_classicFrame_keepsItsFlags(void)
{
    /* arrange: a DS4 or any other Classic pad, on the same host */
    link_t *l = le_link_with_buffer(27, 8);
    l->le = 0;
    unsigned char d[48];
    fill(d, sizeof d);

    /* act */
    int ok = l2_send(&g_h, l, 0x0040, d, sizeof d);

    /* assert */
    CHECK(ok && nsent == 1 && le16(sent[0]) == 0x2010, "the LE buffer size does not touch Classic");
    CHECK(g_h.br.credits == 5 && g_h.lep.credits == 8, "a Classic frame takes a Classic buffer");
}

static void test_l2_send_tooFewBuffers_sendsNothing(void)
{
    /* arrange */
    link_t *l = le_link_with_buffer(27, 2);
    unsigned char key[65];
    fill(key, sizeof key);

    /* act */
    int ok = l2_send(&g_h, l, CID_SMP, key, sizeof key);

    /* assert */
    CHECK(!ok && nsent == 0, "half a frame must not go out, got %d packets", nsent);
    CHECK(g_h.lep.credits == 2, "no buffer taken");
}

static void test_l2_send_failedPiece_reportsIt(void)
{
    /* arrange */
    link_t *l = le_link_with_buffer(27, 8);
    unsigned char key[65];
    fill(key, sizeof key);
    acl_fails_at = 1;

    /* act */
    int ok = l2_send(&g_h, l, CID_SMP, key, sizeof key);

    /* assert */
    CHECK(!ok && nsent == 1, "stops at the piece that failed, got %d", nsent);
    CHECK(g_h.lep.credits == 7, "only the piece sent holds a buffer, %d left", g_h.lep.credits);
}

int main(void)
{
    test_l2_send_keyOn27ByteBuffer_goesInPieces();
    test_l2_send_exactFit_staysOnePacket();
    test_l2_send_keyOnBigBuffer_staysOnePacket();
    test_l2_send_classicFrame_keepsItsFlags();
    test_l2_send_tooFewBuffers_sendsNothing();
    test_l2_send_failedPiece_reportsIt();
    printf("test_bt_l2: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
