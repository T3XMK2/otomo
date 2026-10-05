/* See proto.h. Every multi-byte field is copied byte by byte (records may be read at any alignment; no FPU use). */
#include "proto.h"

typedef struct { uint8_t *p; int n; } W;
static void u8(W *w, uint32_t v) { w->p[w->n++] = (uint8_t)v; }
static void u16(W *w, uint32_t v) { u8(w, v); u8(w, v >> 8); }
static void u32(W *w, uint32_t v) { u16(w, v); u16(w, v >> 16); }
static void raw(W *w, const uint8_t *src, int k) { for (int i = 0; i < k; i++) w->p[w->n++] = src[i]; }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void head(W *w, uint8_t type, uint32_t seq) { u16(w, OT_MAGIC); u8(w, OT_VERSION); u8(w, type); u32(w, seq); }

int encode_sense(uint8_t *out, uint32_t seq, const Facts *f) {
    W w = {out, 0};
    head(&w, T_SENSE, seq);
    u32(&w, f->body_ms); u32(&w, f->timer); u8(&w, f->lease_ok ? 1 : 0); u8(&w, 0);
    u16(&w, f->lost_total);
    const uint8_t *c = f->cat;                  /* _CAT <BBHhhHh3fBB4BIQff */
    u8(&w, c[R_BEHAVIOUR]); u8(&w, c[R_PHASE]);
    raw(&w, c + R_ANIM, 2); raw(&w, c + R_HP, 2); raw(&w, c + R_HP_MAX, 2); raw(&w, c + R_ZONE, 2);
    raw(&w, c + R_PARALYSIS, 2); raw(&w, c + R_POS, 12);
    u8(&w, c[R_LEVEL]); u8(&w, c[R_ATTACK_TYPE]); raw(&w, c + R_WEIGHTS, 4); raw(&w, c + R_TIER, 4);
    raw(&w, c + R_SKILLS, 8); raw(&w, c + R_ATTACK, 4); raw(&w, c + R_DEFENCE, 4);
    const uint8_t *h = f->hunter;               /* _HUNTER <HHBBBBhHH3f; none: zone 0xFFFF, the rest 0 */
    if (h) {
        raw(&w, h + R_ZONE, 2); raw(&w, h + R_HP, 2); u8(&w, h[H_ACT_TYPE]); u8(&w, h[H_ACT_ID]); u8(&w, h[H_CROUCH]);
        u8(&w, 0); raw(&w, h + H_POISON, 2); raw(&w, h + H_ATTACK_UP, 2); raw(&w, h + H_DEFENCE_UP, 2);
        raw(&w, h + R_POS, 12);
    } else {
        u16(&w, 0xFFFF);
        for (int i = 0; i < 24; i++) u8(&w, 0);
    }
    int nm = f->n_mon < (int)MAX_MONSTERS ? f->n_mon : (int)MAX_MONSTERS;
    u8(&w, (uint32_t)nm);
    for (int i = 0; i < nm; i++) {              /* _MONSTER <HHHBBBB3f */
        const uint8_t *m = f->mon[i];
        u16(&w, (f->mon_addr[i] >> 4) & 0xFFFF);
        raw(&w, m + R_ZONE, 2); raw(&w, m + R_HP, 2); u8(&w, m[M_ACT_TYPE]); u8(&w, m[M_ACT_TYPE + 1]);
        u8(&w, m[M_SPECIES]); u8(&w, (rd32(m + M_FLAGS) & M_LARGE) ? 1 : 0); raw(&w, m + R_POS, 12);
    }
    int p0 = f->n_prop > (int)MAX_PROPOSALS ? f->n_prop - (int)MAX_PROPOSALS : 0;     /* the last 16 */
    u8(&w, (uint32_t)(f->n_prop - p0));
    for (int i = p0; i < f->n_prop; i++) u8(&w, f->prop[i]);
    int h0 = f->n_hit > (int)MAX_HITS ? f->n_hit - (int)MAX_HITS : 0;                /* the last 8 */
    u8(&w, (uint32_t)(f->n_hit - h0));
    for (int i = h0; i < f->n_hit; i++) { u8(&w, f->hit[i].who); u8(&w, f->hit[i].idx); u16(&w, f->hit[i].dmg); }
    int no = f->n_obj < (int)MAX_OBJECTS ? f->n_obj : (int)MAX_OBJECTS;              /* the first 4 */
    u8(&w, (uint32_t)no);
    for (int i = 0; i < no; i++) {
        u16(&w, (f->obj[i].addr >> 4) & 0xFFFF); u8(&w, f->obj[i].owner); u8(&w, f->obj[i].trap);
        u32(&w, f->obj[i].x); u32(&w, f->obj[i].z);
    }
    return w.n;
}

int encode_hello_body(uint8_t *out, uint32_t seq, const char *game, uint8_t kind, uint16_t version,
                      const uint8_t *card, uint8_t training) {
    W w = {out, 0};
    head(&w, T_HELLO, seq);
    int i = 0;
    for (; i < 10 && game[i]; i++) u8(&w, (uint8_t)game[i]);
    for (; i < 10; i++) u8(&w, 0);
    u8(&w, kind); u16(&w, version);
    if (card) { raw(&w, card, 0x70); u8(&w, training); }   /* _HELLO_BODY_CARD <112sB: the neural cat's body */
    return w.n;
}

static void text(W *w, const char *s, int k) {
    int i = 0;
    for (; i < k && s && s[i]; i++) u8(w, (uint8_t)s[i]);
    for (; i < k; i++) u8(w, 0);
}

int encode_bye_body(uint8_t *out, uint32_t seq, const char *quest, const char *run, uint8_t why) {
    W w = {out, 0};
    head(&w, T_BYE, seq);
    text(&w, quest, 16); text(&w, run, 32); u8(&w, why);
    return w.n;
}

int decode_mind(const uint8_t *in, int n, uint32_t *seq, TableMsg *t, HelloMind *h) {
    if (n < 8 || (in[0] | (in[1] << 8)) != OT_MAGIC || in[2] != OT_VERSION) return -1;
    *seq = rd32(in + 4);
    const uint8_t *p = in + 8;
    int type = in[3];
    if (type == (int)T_TABLE) {                 /* _TABLE <IHBB64s4BQ */
        if (n < (int)TABLE_MSG_SIZE || !t) return -1;
        t->ack = rd32(p); t->lease_ms = (uint16_t)(p[4] | (p[5] << 8)); t->attached = p[6]; t->has_card = p[7] & 1;
        for (int i = 0; i < (int)TABLE_SIZE; i++) t->table[i] = p[8 + i];
        for (int i = 0; i < 4; i++) t->weights[i] = p[8 + TABLE_SIZE + i];
        t->skills_lo = rd32(p + 12 + TABLE_SIZE); t->skills_hi = rd32(p + 16 + TABLE_SIZE);
        return type;
    }
    if (type == (int)T_HELLO) {                 /* _HELLO_MIND <H16s (+ _HELLO_CARD <4BQ) */
        if (n < 8 + 18 || !h) return -1;
        h->version = (uint16_t)(p[0] | (p[1] << 8));
        for (int i = 0; i < 16; i++) h->name[i] = (char)p[2 + i];
        h->name[16] = 0;
        h->has_card = n >= 8 + 18 + 12;
        if (h->has_card) {
            for (int i = 0; i < 4; i++) h->weights[i] = p[18 + i];
            h->skills_lo = rd32(p + 22); h->skills_hi = rd32(p + 26);
        }
        return type;
    }
    if (type == (int)T_BYE) return type;
    return -1;
}
