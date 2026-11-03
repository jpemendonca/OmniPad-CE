#ifndef BT_SCHED_H
#define BT_SCHED_H

#include <stdint.h>

/* Who gets the adapter's radio and when: the turn of HCI commands and the background search.
 * Apart from bt_adapter.c because that file needs USB, and these rules have to run in host tests. */

#define BT_CMDQ_LEN 32
#define BT_CMD_REPLY_WAIT_MS 500  /* a controller that never gives the turn back gets the next command anyway */

typedef struct {
    uint16_t opcode;
    uint8_t  len;
    uint8_t  params[255];
} bt_cmd_t;

typedef struct {
    int      credits;   /* commands the controller can take now (Num_HCI_Command_Packets) */
    long     sent_at;
    bt_cmd_t q[BT_CMDQ_LEN];
    int      head, count;
} bt_cmdq_t;

/* Sends one command to the controller. Returns 0 once the transport is gone. */
typedef int (*bt_cmdq_send_fn)(void *ctx, uint16_t opcode, const uint8_t *params, int len);

/* Empties the queue and gives the controller one credit, as it has when it starts. */
void bt_cmdq_reset(bt_cmdq_t *q);

/* Adds a command at the end. Returns 0 if the queue is full or plen is out of range. */
int  bt_cmdq_push(bt_cmdq_t *q, unsigned opcode, const void *params, int plen);

/* Takes the credits from an HCI event (code first). Call for every event that arrives. */
void bt_cmdq_on_event(bt_cmdq_t *q, const uint8_t *ev, int len);

/* Sends waiting commands while the controller has room. */
void bt_cmdq_flush(bt_cmdq_t *q, bt_cmdq_send_fn send, void *ctx);

enum { BT_AUTO_OFF, BT_AUTO_FULL, BT_AUTO_PEEK };

#define BT_AUTO_FULL_S    600        /* renewed while no pad of ours is connected */
#define BT_AUTO_FULL_INQ  4          /* inquiry rounds of 5.12 s, taking turns with the LE scan */
#define BT_AUTO_PEEK_S    4          /* with a pad connected: a 3.84 s round ... */
#define BT_AUTO_PEEK_INQ  3
#define BT_AUTO_PEEK_MS   20000      /* ... every 20 s */

/* Which background search runs now. linked counts pads with a link up, ready or not. */
int  bt_auto_mode(int slot_free, int rest_quiet, int linked, int fake_csr);

#endif /* BT_SCHED_H */
