// The cat's MIND, inference part, in C++ (the C3's own copy of the lab's {senses,profile,brain,neuro}.py):
// facts (protocol.h Sense) -> senses vector (59) -> owner style (6) + owner profile (18) -> the neural brain -> the
// decision table, with the same safety rules as the Python brain. No Arduino code: the same file builds on the C3 and
// natively on the PC, where the lab's mind_parity.py checks it against Python frame by frame.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "protocol.h"

namespace otomo {

const int VECTOR_LEN = 59, N_BEH = 46, N_STYLE = 6, N_PROF = 18, N_IN = VECTOR_LEN + N_BEH + N_STYLE + N_PROF;
const int N_HID = 32, N_OUT = N_BEH;
const uint8_t EMPTY = 0xFF;
const float GAME_SHARE = 0.85f, CAP = 2.0f, FLOOR_P = 0.005f;

// ------------------------------------------------------------------------------------------- senses (senses.py)
enum Category { C_CHOICE, C_SUPPORT, C_COMBAT, C_CONTEXT, C_TERRAIN, C_MOVE, C_REACTION, N_CATEGORIES };
inline int category(int b) {
    switch (b) {
        case 0: case 1: case 2: case 17: return C_CHOICE;
        case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 29: return C_SUPPORT;
        case 15: case 16: case 25: case 26: case 27: case 28: return C_COMBAT;
        case 10: case 11: case 12: case 13: case 14: case 20: return C_CONTEXT;
        case 19: case 23: case 24: return C_TERRAIN;
        case 18: case 21: case 22: return C_MOVE;
        default: return C_REACTION;                       // 30..45 and anything unknown (Python: CATEGORY.get(b, 'reaction'))
    }
}
// HUNTER_ACTIONS order
enum HAct { A_IDLE, A_WALK, A_RUN, A_BUMP, A_CROUCH, A_STAND_UP, A_CROUCH_WALK, A_CRAWL, A_BED, A_GATHER, A_CARVE,
            A_EAT_DRINK, A_BBQ, A_FISH, A_DRAWN, A_WEAPON, A_HIT, A_OTHER, N_HACT };
inline int hunter_action(int type, int id) {
    if (type == 1) return A_WEAPON;
    if (type == 2) return A_HIT;
    if (type != 0) return A_OTHER;
    switch (id) {
        case 0: return A_IDLE; case 1: case 31: return A_WALK; case 36: return A_RUN; case 44: return A_BUMP;
        case 8: return A_CROUCH; case 9: return A_STAND_UP; case 59: case 60: case 29: return A_CROUCH_WALK;
        case 61: return A_CRAWL; case 51: case 52: case 53: return A_BED; case 71: return A_GATHER; case 74: return A_CARVE;
        case 88: case 117: return A_EAT_DRINK; case 76: case 85: case 75: case 77: return A_BBQ;
        case 79: case 80: case 83: return A_FISH; case 3: case 10: case 5: return A_DRAWN;
        default: return A_OTHER;
    }
}
const int KEY_SKILLS[10] = {0, 1, 2, 3, 4, 8, 20, 26, 29, 30};
// where things are in the vector (senses.V_*)
const int V_HUNTER = 5 + 4 + 10 + N_CATEGORIES + 3, V_HUNTER_HP = V_HUNTER + 2;

struct MonsterView { double dist; bool large, attacking; };

struct State {                                            // senses.CatState, the parts the brain uses
    const Sense *s;
    bool hunter_same_zone = false; double hunter_dist = -1; int hunter_act = A_OTHER;
    MonsterView mon[MAX_MONSTERS]; int n_mon = 0;
};

inline void state_from_sense(const Sense &s, State &st) {
    st.s = &s;
    const Cat &c = s.cat;
    double cx = c.pos[0], cz = c.pos[2];
    st.hunter_same_zone = false; st.hunter_dist = -1; st.hunter_act = A_OTHER;
    if (s.hunter.present) {
        st.hunter_same_zone = s.hunter.zone == c.zone;
        if (st.hunter_same_zone) st.hunter_dist = hypot((double)s.hunter.pos[0] - cx, (double)s.hunter.pos[2] - cz);
        st.hunter_act = hunter_action(s.hunter.act_type, s.hunter.act_id);
    }
    st.n_mon = 0;
    for (int i = 0; i < s.n_mon; i++) {
        const Mon &m = s.mon[i];
        if (m.zone != c.zone || m.hp == 0) continue;
        MonsterView v{hypot((double)m.pos[0] - cx, (double)m.pos[2] - cz), m.large != 0, m.act_type == 3};
        int k = st.n_mon++;                               // insertion sort by distance (stable, like Python's sorted)
        while (k > 0 && st.mon[k - 1].dist > v.dist) { st.mon[k] = st.mon[k - 1]; k--; }
        st.mon[k] = v;
    }
}

inline void vector(const State &st, float v[VECTOR_LEN]) {
    const Sense &s = *st.s;
    const Cat &c = s.cat;
    int k = 0;
    v[k++] = (float)c.hp / (float)(c.hp_max > 1 ? c.hp_max : 1);
    v[k++] = c.hp <= 0 ? 1.f : 0.f;
    v[k++] = c.level / 20.f;
    v[k++] = c.tier / 5.f;
    v[k++] = (float)c.attack_type;
    for (int i = 0; i < 4; i++) v[k++] = (c.weights[i] != 255 ? c.weights[i] : -1) / 100.f;
    for (int i = 0; i < 10; i++) v[k++] = (c.skills >> KEY_SKILLS[i]) & 1 ? 1.f : 0.f;
    int cat = category(c.behaviour);
    for (int i = 0; i < N_CATEGORIES; i++) v[k++] = cat == i ? 1.f : 0.f;
    v[k++] = c.paralysis > 0 ? 1.f : 0.f;
    v[k++] = c.behaviour == 35 ? 1.f : 0.f;
    v[k++] = c.behaviour == 36 ? 1.f : 0.f;
    // the hunter (none: not same zone, distance 5000, HP 0, action 'other')
    const Hunter &h = s.hunter;
    v[k++] = st.hunter_same_zone ? 1.f : 0.f;
    double d = st.hunter_dist < 0 ? 5000.0 : (st.hunter_dist < 5000.0 ? st.hunter_dist : 5000.0);
    v[k++] = (float)(d / 5000.0);
    v[k++] = h.present ? h.hp / 150.f : 0.f;
    v[k++] = h.present && h.poison > 0 ? 1.f : 0.f;
    v[k++] = h.present && h.attack_up > 0 ? 1.f : 0.f;
    v[k++] = h.present && h.defence_up > 0 ? 1.f : 0.f;
    for (int i = 0; i < N_HACT; i++) v[k++] = st.hunter_act == i ? 1.f : 0.f;
    for (int large = 1; large >= 0; large--) {
        int n = 0, first = -1;
        for (int i = 0; i < st.n_mon; i++) if (st.mon[i].large == (large == 1)) { n++; if (first < 0) first = i; }
        v[k++] = (float)n;
        v[k++] = first < 0 ? 1.f : (float)(st.mon[first].dist / 5000.0);
        v[k++] = first < 0 ? 0.f : (st.mon[first].attacking ? 1.f : 0.f);
    }
}

inline float round4(float x) { return roundf(x * 10000.f) / 10000.f; }
// Python's round(d, 3): decided on d's EXACT binary value. Clear of a decimal tie the plain sum is the same; next to
// one, the C library's printf (David Gay's correctly rounded conversion, as Python's) decides.
inline double round3(double d) {
    double y = d * 1000.0, f = floor(y);
    if (fabs(y - f - 0.5) > 1e-6) return floor(y + 0.5) / 1000.0;
    char b[40];
    snprintf(b, sizeof b, "%.3f", d);
    return strtod(b, nullptr);
}

// ------------------------------------------------------------------------------------ the owner (profile.py, neuro.owner_style)
// The owner's windows are FIXED rings, not deques (5 Oct: 180 s of frames in std::deque held ~40 KB of the C3's heap
// through a quest and after it, down to 11 KB free with the page open). Times are the body's ms: t = ms / 1000.0 is
// exactly the double the Python side compares. Sized for ~5 SENSE/s with room; a faster body loses its OLDEST
// samples and says so (overflow, in /status) instead of growing.
template <typename T, int N> struct Ring {
    T v[N]; int head = 0, n = 0; uint32_t overflow = 0;
    static int wrap(int j) { return j < N ? j : j - N; }   // no modulo: the C3 has no divider worth using per sample
    void push_back(const T &x) {
        if (n == N) { head = wrap(head + 1); n--; overflow++; }
        v[wrap(head + n++)] = x;
    }
    bool empty() const { return n == 0; }
    int size() const { return n; }
    const T &front() const { return v[head]; }
    const T &back() const { return v[wrap(head + n - 1)]; }
    const T &operator[](int i) const { return v[wrap(head + i)]; }
    void pop_front() { head = wrap(head + 1); n--; }
    void clear() { head = n = 0; overflow = 0; }
};
inline double secs(uint32_t ms) { return ms / 1000.0; }
// the first ms whose secs(ms) >= lo - so a window test is an integer compare with EXACTLY the double one's answer
// (the C3 has no FPU: a double division per sample cost 5 ms an answer)
inline uint32_t first_ms(double lo) {
    if (lo <= 0) return 0;
    if (lo >= 4294967.0) return 0xFFFFFFFFu;
    uint32_t m = (uint32_t)ceil(lo * 1000.0);
    while (m > 0 && secs(m - 1) >= lo) m--;               // bounded: one or two steps at most
    while (m < 0xFFFFFFFFu && !(secs(m) >= lo)) m++;
    return m;
}

struct OwnerStyle {                                      // shares of his actions in the last 120 s (reward.owner_style)
    struct E { uint32_t ms; uint8_t act; } __attribute__((packed));
    Ring<E, 800> buf;                                    // 120 s at 6.7 frames/s, 4 KB
    void observe(uint32_t ms, int act) {
        buf.push_back({ms, (uint8_t)act});
        double t = secs(ms);
        while (!buf.empty() && secs(buf.front().ms) < t - 120.0) buf.pop_front();
    }
    void clear() { buf.clear(); }
    int counts(int c[N_STYLE]) const {                    // attacks, gets_hit, heals, gathers, crouches, runs
        for (int i = 0; i < N_STYLE; i++) c[i] = 0;
        for (int i = 0; i < buf.size(); i++) {
            switch (buf[i].act) {
                case A_WEAPON: case A_DRAWN: c[0]++; break;
                case A_HIT: c[1]++; break;
                case A_EAT_DRINK: c[2]++; break;
                case A_GATHER: case A_CARVE: c[3]++; break;
                case A_CROUCH: case A_CROUCH_WALK: case A_CRAWL: c[4]++; break;
                case A_RUN: c[5]++; break;
                default: break;
            }
        }
        return buf.empty() ? 1 : buf.size();
    }
    void style(float out[N_STYLE]) const {               // neuro.owner_style: what the brain DECIDES with
        int c[N_STYLE], n = counts(c);
        for (int i = 0; i < N_STYLE; i++) out[i] = (float)c[i] / n;
    }
    // reward.owner_style, the shares a LIVED decision is learnt with: rounded to 3 decimals (5 Oct: the C3 learnt
    // with the raw shares - up to 5e-4 off, and 'heals' read as 'calm' at the 0.15 line of habits.context)
    void style_rounded(float out[N_STYLE]) const {
        int c[N_STYLE], n = counts(c);
        for (int i = 0; i < N_STYLE; i++) out[i] = (float)round3((double)c[i] / n);
    }
};

struct OwnerProfile {                                    // profile.OwnerProfile, windows 30 s and 180 s
    enum { F_ATT = 1, F_LOW = 2, F_GAT = 4, F_RUN = 8, F_ALONE = 16 };
    struct Sample { uint32_t ms; float drop, dist; uint8_t f; } __attribute__((packed));
    struct Heal { uint32_t ms; float hp; };
    Ring<Sample, 1200> samples;                          // 180 s at 6.7 frames/s, 15.6 KB
    Ring<Heal, 64> heals;
    Ring<uint32_t, 768> hits;                            // his hits: 180 s at 4.3 a second
    bool has_last = false; float last_hp = 0; int last_act = -1;

    void clear() { samples.clear(); heals.clear(); hits.clear(); has_last = false; last_hp = 0; last_act = -1; }
    uint32_t overflow() const { return samples.overflow + heals.overflow + hits.overflow; }
    // v = the ROUNDED vector of the row (Python's row['vector'] is rounded to 4 decimals)
    void update(uint32_t ms, const float v[VECTOR_LEN], int act) {
        float hp = v[V_HUNTER_HP];
        bool same = v[V_HUNTER] > 0;
        float dist = same ? v[V_HUNTER + 1] : 1.f;
        float drop = has_last ? fmaxf(0.f, last_hp - hp) : 0.f;
        bool attacking = act == A_WEAPON || act == A_DRAWN;
        uint8_t f = (attacking ? F_ATT : 0) | (hp < 0.4f ? F_LOW : 0) | (act == A_GATHER || act == A_CARVE ? F_GAT : 0)
                    | (act == A_RUN ? F_RUN : 0) | (attacking && dist > 0.3f ? F_ALONE : 0);
        samples.push_back({ms, drop, dist, f});
        if (act == A_EAT_DRINK && last_act != A_EAT_DRINK) heals.push_back({ms, hp});
        last_hp = hp; last_act = act; has_last = true;
        double horizon = secs(ms) - 180.0;
        while (!samples.empty() && secs(samples.front().ms) < horizon) samples.pop_front();
        while (!heals.empty() && secs(heals.front().ms) < horizon) heals.pop_front();
        while (!hits.empty() && secs(hits.front()) < horizon) hits.pop_front();
    }
    void hit(uint32_t ms) { hits.push_back(ms); }
    void features(float out[N_PROF]) const {
        double now = samples.empty() ? 0.0 : secs(samples.back().ms);
        const double W[2] = {30.0, 180.0};
        int k = 0;
        for (double w : W) {
            double lo = now - w;
            uint32_t m0 = first_ms(lo);
            double s_att = 0, s_drop = 0, s_low = 0, s_gat = 0, s_run = 0, s_dist = 0, s_alone = 0;
            int n = 0;
            for (int i = 0; i < samples.size(); i++) {
                const Sample &s = samples[i];
                if (s.ms < m0) continue;
                n++; s_att += (s.f & F_ATT) != 0; s_drop += s.drop; s_low += (s.f & F_LOW) != 0; s_gat += (s.f & F_GAT) != 0;
                s_run += (s.f & F_RUN) != 0; s_dist += s.dist; s_alone += (s.f & F_ALONE) != 0;
            }
            int nn = n > 0 ? n : 1;
            double span_min = (w > 1.0 ? w : 1.0) / 60.0;
            int nh = 0; for (int i = 0; i < hits.size(); i++) if (hits[i] >= m0) nh++;
            double heal_sum = 0; int heal_n = 0;
            for (int i = 0; i < heals.size(); i++) if (heals[i].ms >= m0) { heal_sum += heals[i].hp; heal_n++; }
            double f[9] = {s_att / nn, fmin(1.5, nh / span_min / 10.0), fmin(1.5, s_drop / span_min),
                           heal_n ? heal_sum / heal_n : 0.5, s_low / nn, s_gat / nn, s_run / nn,
                           n ? s_dist / nn : 1.0, s_alone / nn};
            for (int i = 0; i < 9; i++) out[k++] = round4((float)f[i]);
        }
    }
};

// ------------------------------------------------------------------------------------------- options (brain.py)
// the answers the brain may give to a proposal, ascending; returns the count
inline int allowed(int p, const Cat &c, uint8_t out[N_BEH]) {
    bool set[N_BEH] = {false};
    auto has = [&](int bit) { return (c.skills >> bit) & 1; };
    set[p] = true;
    bool combat = p == 15 || p == 16 || p == 25 || p == 26 || p == 27 || p == 28;
    bool free_choice = p == 1 || p == 2 || p == 17;
    bool support = (p >= 3 && p <= 9) || p == 29;
    if (combat) {
        int ws = c.weights[0];                           // 255 pacifist, 0 bombs only, 100 weapon only
        set[16] = set[17] = true;
        if (ws == 255) set[15] = true;
        else {
            if (ws > 0) set[25] = set[26] = true;
            if (ws < 100) { set[27] = true; if (has(4)) set[28] = true; }
        }
    } else if (free_choice) {
        set[17] = true;
        const int SB[7][2] = {{3, 0}, {4, 1}, {6, 2}, {7, 3}, {8, 8}, {5, 20}, {9, 26}};   // behaviour, skill bit
        for (auto &sb : SB) if (has(sb[1])) set[sb[0]] = true;
        if (!has(30)) set[2] = true;                     // Workaholic never slacks
    } else if (support) {
        set[17] = true;
    }
    int n = 0;
    for (int b = 0; b < N_BEH; b++) if (set[b]) out[n++] = (uint8_t)b;
    return n;
}

inline bool needs_target(int b) { return b == 15 || b == 16 || b == 25 || b == 26 || b == 27 || b == 28; }

// ------------------------------------------------------------------------------------------- the brain (neuro.py)
struct Brain {
    float W1[N_HID][N_IN], b1[N_HID], W2[N_OUT][N_HID], b2[N_OUT];
};

struct Mind {
    const Brain *brain;
    OwnerStyle style;
    OwnerProfile profile;
    float base[N_HID];                                   // W1 . (inputs without the proposal one-hot) + b1, per frame
    float temp = 1.f;

    // once per frame: everything but the proposal
    void prepare(const float v[VECTOR_LEN], const float st[N_STYLE], const float pf[N_PROF], const Cat &c) {
        for (int j = 0; j < N_HID; j++) {
            const float *w = brain->W1[j];
            float a = brain->b1[j];
            for (int i = 0; i < VECTOR_LEN; i++) a += w[i] * v[i];
            for (int i = 0; i < N_STYLE; i++) a += w[VECTOR_LEN + N_BEH + i] * st[i];
            for (int i = 0; i < N_PROF; i++) a += w[VECTOR_LEN + N_BEH + N_STYLE + i] * pf[i];
            base[j] = a;
        }
        temp = 0.7f + 0.6f * (c.weights[1] / 100.f);
    }
    // the probabilities over opts (ascending) for proposal p
    void probs(int p, const uint8_t *opts, int n, float *out) const {
        float h[N_HID];
        for (int j = 0; j < N_HID; j++) h[j] = tanhf(base[j] + brain->W1[j][VECTOR_LEN + p]);
        float other = logf((1.f - GAME_SHARE) / (float)(n > 1 ? n - 1 : 1)), mine = logf(GAME_SHARE), mx = -1e30f;
        for (int k = 0; k < n; k++) {
            int a = opts[k];
            float z = brain->b2[a];
            for (int j = 0; j < N_HID; j++) z += brain->W2[a][j] * h[j];
            z = z > CAP ? CAP : (z < -CAP ? -CAP : z);
            out[k] = ((a == p ? mine : other) + z) / temp;
            if (out[k] > mx) mx = out[k];
        }
        float sum = 0, mn = 1;
        for (int k = 0; k < n; k++) { out[k] = expf(out[k] - mx); sum += out[k]; }
        for (int k = 0; k < n; k++) { out[k] /= sum; if (out[k] < mn) mn = out[k]; }
        if (mn < FLOOR_P) {                               // never forbidden
            sum = 0;
            for (int k = 0; k < n; k++) { if (out[k] < FLOOR_P) out[k] = FLOOR_P; sum += out[k]; }
            for (int k = 0; k < n; k++) out[k] /= sum;
        }
    }
    // the table; uniform() gives the draws in [0, 1) (Python: rng.random(), one per proposal with options)
    template <typename U> void table(const Cat &c, U uniform, uint8_t table[TABLE_SIZE]) const {
        memset(table, EMPTY, TABLE_SIZE);
        uint8_t opts[N_BEH];
        float pr[N_BEH];
        for (int p = 0; p < N_BEH; p++) {
            int n = allowed(p, c, opts);
            if (n == 1) continue;
            probs(p, opts, n, pr);
            float r = uniform(), acc = 0;
            int answer = p;
            for (int k = 0; k < n; k++) { acc += pr[k]; if (r <= acc) { answer = opts[k]; break; } }
            if (answer == p) continue;
            if (needs_target(answer) && !needs_target(p)) continue;   // the hook's safety rule (hook.table_violations)
            if (answer == 1 && p != 1) continue;                      // never sent to gather (brain.build_table)
            table[p] = (uint8_t)answer;
        }
    }
};

}  // namespace otomo
