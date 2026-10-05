// The Felyne protocol v1 on the C3 (docs/FELYNE_PROTOCOL.md; Python reference: the lab's protocol.py).
// Little-endian, packed; readers take bytes with memcpy (the C3 is little-endian too, but never assume alignment).
#pragma once
#include <stdint.h>
#include <string.h>

namespace otomo {

const uint16_t MAGIC = 0x544F;                           // 'OT'
const uint8_t VERSION = 1, T_HELLO = 0, T_SENSE = 1, T_TABLE = 2, T_BYE = 3;
const int TABLE_SIZE = 64, MAX_MONSTERS = 20, MAX_PROPOSALS = 16, MAX_HITS = 8, MAX_OBJECTS = 4;

struct Cat {
    uint8_t behaviour, phase; uint16_t anim; int16_t hp, hp_max; uint16_t zone; int16_t paralysis; float pos[3];
    uint8_t level, attack_type, weights[4]; uint32_t tier; uint64_t skills; float attack, defence;
};
struct Hunter {
    bool present; uint16_t zone, hp; uint8_t act_type, act_id, crouch; int16_t poison; uint16_t attack_up, defence_up;
    float pos[3];
};
struct Mon { uint16_t key, zone, hp; uint8_t act_type, act_id, species, large; float pos[3]; };
struct Hit { uint8_t who, monster; uint16_t damage; };
struct Obj { uint16_t key; uint8_t owner, trap; float x, z; };
struct Sense {
    uint32_t seq, body_ms, timer; uint8_t lease_ok; uint16_t lost_total;
    Cat cat; Hunter hunter;
    uint8_t n_mon; Mon mon[MAX_MONSTERS];
    uint8_t n_prop; uint8_t prop[MAX_PROPOSALS];          // bit 7 = lost
    uint8_t n_hit; Hit hit[MAX_HITS];
    uint8_t n_obj; Obj obj[MAX_OBJECTS];
};

struct Reader {                                          // bounded: a short datagram fails, never overreads
    const uint8_t *p; int n, at; bool ok;
    Reader(const uint8_t *d, int len) : p(d), n(len), at(0), ok(true) {}
    template <typename T> T get() { T v{}; if (at + (int)sizeof(T) > n) { ok = false; return v; } memcpy(&v, p + at, sizeof(T)); at += sizeof(T); return v; }
};

// -> the message type, or -1 if this is not a v1 datagram
inline int decode_sense(const uint8_t *d, int len, Sense &s) {
    Reader r(d, len);
    if (r.get<uint16_t>() != MAGIC || r.get<uint8_t>() != VERSION) return -1;
    uint8_t type = r.get<uint8_t>();
    s.seq = r.get<uint32_t>();
    if (type != T_SENSE) return r.ok ? type : -1;
    s.body_ms = r.get<uint32_t>(); s.timer = r.get<uint32_t>(); s.lease_ok = r.get<uint8_t>(); r.get<uint8_t>();
    s.lost_total = r.get<uint16_t>();
    Cat &c = s.cat;
    c.behaviour = r.get<uint8_t>(); c.phase = r.get<uint8_t>(); c.anim = r.get<uint16_t>(); c.hp = r.get<int16_t>();
    c.hp_max = r.get<int16_t>(); c.zone = r.get<uint16_t>(); c.paralysis = r.get<int16_t>();
    for (int k = 0; k < 3; k++) c.pos[k] = r.get<float>();
    c.level = r.get<uint8_t>(); c.attack_type = r.get<uint8_t>();
    for (int k = 0; k < 4; k++) c.weights[k] = r.get<uint8_t>();
    c.tier = r.get<uint32_t>(); c.skills = r.get<uint64_t>(); c.attack = r.get<float>(); c.defence = r.get<float>();
    Hunter &h = s.hunter;
    h.zone = r.get<uint16_t>(); h.hp = r.get<uint16_t>(); h.act_type = r.get<uint8_t>(); h.act_id = r.get<uint8_t>();
    h.crouch = r.get<uint8_t>(); r.get<uint8_t>(); h.poison = r.get<int16_t>(); h.attack_up = r.get<uint16_t>();
    h.defence_up = r.get<uint16_t>();
    for (int k = 0; k < 3; k++) h.pos[k] = r.get<float>();
    h.present = h.zone != 0xFFFF;
    s.n_mon = r.get<uint8_t>(); if (s.n_mon > MAX_MONSTERS) return -1;
    for (int i = 0; i < s.n_mon; i++) {
        Mon &m = s.mon[i];
        m.key = r.get<uint16_t>(); m.zone = r.get<uint16_t>(); m.hp = r.get<uint16_t>(); m.act_type = r.get<uint8_t>();
        m.act_id = r.get<uint8_t>(); m.species = r.get<uint8_t>(); m.large = r.get<uint8_t>();
        for (int k = 0; k < 3; k++) m.pos[k] = r.get<float>();
    }
    s.n_prop = r.get<uint8_t>(); if (s.n_prop > MAX_PROPOSALS) return -1;
    for (int i = 0; i < s.n_prop; i++) s.prop[i] = r.get<uint8_t>();
    s.n_hit = r.get<uint8_t>(); if (s.n_hit > MAX_HITS) return -1;
    for (int i = 0; i < s.n_hit; i++) { s.hit[i].who = r.get<uint8_t>(); s.hit[i].monster = r.get<uint8_t>(); s.hit[i].damage = r.get<uint16_t>(); }
    s.n_obj = r.get<uint8_t>(); if (s.n_obj > MAX_OBJECTS) return -1;
    for (int i = 0; i < s.n_obj; i++) { Obj &o = s.obj[i]; o.key = r.get<uint16_t>(); o.owner = r.get<uint8_t>(); o.trap = r.get<uint8_t>(); o.x = r.get<float>(); o.z = r.get<float>(); }
    return r.ok ? T_SENSE : -1;
}

struct Writer {
    uint8_t *p; int at;
    explicit Writer(uint8_t *d) : p(d), at(0) {}
    template <typename T> void put(T v) { memcpy(p + at, &v, sizeof(T)); at += sizeof(T); }
};

// TABLE: ack, lease ms, attached, flags (bit 0 = card), 64 answers (0xFF = the game's own), card weights, skills
inline int encode_table(uint8_t *out, uint32_t seq, uint32_t ack, uint16_t lease_ms, bool attached,
                        const uint8_t table[TABLE_SIZE], const uint8_t *card_weights = nullptr, uint64_t skills = 0) {
    Writer w(out);
    w.put<uint16_t>(MAGIC); w.put<uint8_t>(VERSION); w.put<uint8_t>(T_TABLE); w.put<uint32_t>(seq);
    w.put<uint32_t>(ack); w.put<uint16_t>(lease_ms); w.put<uint8_t>(attached ? 1 : 0); w.put<uint8_t>(card_weights ? 1 : 0);
    memcpy(out + w.at, table, TABLE_SIZE); w.at += TABLE_SIZE;
    for (int k = 0; k < 4; k++) w.put<uint8_t>(card_weights ? card_weights[k] : 0);
    w.put<uint64_t>(skills);
    return w.at;
}

// name for the HUD; card (optional, 5 Oct): its personality weights + skills, so the body can swap the card in
inline int encode_hello_mind(uint8_t *out, uint32_t seq, uint16_t version, const char *name,
                             const uint8_t *card_weights = nullptr, uint64_t skills = 0) {
    Writer w(out);
    w.put<uint16_t>(MAGIC); w.put<uint8_t>(VERSION); w.put<uint8_t>(T_HELLO); w.put<uint32_t>(seq);
    w.put<uint16_t>(version);
    char nm[16] = {0}; strncpy(nm, name, 16); memcpy(out + w.at, nm, 16); w.at += 16;
    if (card_weights) { for (int k = 0; k < 4; k++) w.put<uint8_t>(card_weights[k]); w.put<uint64_t>(skills); }
    return w.at;
}

// BYE from the body (5 Oct): the quest is over - quest[16], run[32], why (0 end, 1 abandoned); false if not one
inline bool decode_bye_body(const uint8_t *d, int len, char quest[17], char run[33], uint8_t &why) {
    if (len != 8 + 16 + 32 + 1) return false;
    memcpy(quest, d + 8, 16); quest[16] = 0;
    memcpy(run, d + 24, 32); run[32] = 0;
    why = d[56];
    return true;
}
// the mind's answer: lessons lived in the quest, seconds attached, the brain's step
inline int encode_bye_mind(uint8_t *out, uint32_t seq, uint32_t lived, float attached_s, uint32_t step) {
    Writer w(out);
    w.put<uint16_t>(MAGIC); w.put<uint8_t>(VERSION); w.put<uint8_t>(T_BYE); w.put<uint32_t>(seq);
    w.put<uint32_t>(lived); w.put<float>(attached_s); w.put<uint32_t>(step);
    return w.at;
}

}  // namespace otomo
