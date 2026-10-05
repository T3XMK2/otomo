/* Protocol v1 (docs/FELYNE_PROTOCOL.md) on the body's side: raw facts -> SENSE / HELLO / BYE bytes, TABLE / HELLO
 * from the mind. The same bytes as the lab's protocol.py (encode_sense(sense_from_raw(...)) etc.; checked natively
 * by the lab's body_proto_parity.py). Pure: the records are plain pointers (into the game's RAM on the PSP). */
#pragma once
#include <stdint.h>
#include "layout.h"

typedef struct {
    uint32_t body_ms, timer;
    uint8_t lease_ok;
    uint16_t lost_total;
    const uint8_t *cat;                         /* the comrade's record (>= R_PHASE + 1 bytes) */
    const uint8_t *hunter;                      /* the hunter's record, NULL = none */
    int n_mon;                                  /* the game's list, in its order (only the first MAX_MONSTERS go) */
    uint32_t mon_addr[MAX_MONSTERS];
    const uint8_t *mon[MAX_MONSTERS];
    int n_prop;                                 /* proposals, oldest first; bit 7 = lost (only the last 16 go) */
    uint8_t prop[64];
    int n_hit;                                  /* who 0 cat / 1 hunter / 2 other, monster index (255 = none), damage */
    struct { uint8_t who, idx; uint16_t dmg; } hit[64];
    int n_obj;                                  /* traps: node address, owner (as who), trap 0..2, x, z (raw f32 bits) */
    struct { uint32_t addr; uint8_t owner, trap; uint32_t x, z; } obj[MAX_OBJECTS];
} Facts;

typedef struct {
    uint32_t ack;
    uint16_t lease_ms;
    uint8_t attached, has_card;
    uint8_t table[TABLE_SIZE];
    uint8_t weights[4];
    uint32_t skills_lo, skills_hi;
} TableMsg;

typedef struct {
    uint16_t version;
    char name[17];
    uint8_t has_card, weights[4];
    uint32_t skills_lo, skills_hi;
} HelloMind;

int encode_sense(uint8_t *out, uint32_t seq, const Facts *f);                       /* -> length */
/* card = the equipped comrade's hall card (0x70 bytes, raw) or NULL, training = its training at the board (0 = unknown) */
int encode_hello_body(uint8_t *out, uint32_t seq, const char *game, uint8_t kind, uint16_t version,
                      const uint8_t *card, uint8_t training);
int encode_bye_body(uint8_t *out, uint32_t seq, const char *quest, const char *run, uint8_t why);
/* -> the message type (T_TABLE / T_HELLO / T_BYE), or -1 (not a v1 datagram / too short) */
int decode_mind(const uint8_t *in, int n, uint32_t *seq, TableMsg *table, HelloMind *hello);
