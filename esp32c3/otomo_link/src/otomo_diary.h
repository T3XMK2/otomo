// What the cat REMEMBERS of its life, for its owner to read (neuro.Neuro._remember / _note / strongest, habits.context):
// the situations it lived (the latest 40 here - the PC keeps 400, the C3 has no room), the habits that moved most from
// day one, and a diary - a habit born, a habit given up, a choice that felt bad. Not a parity part: it only reads the
// brain, it never changes it.
#pragma once
#include "otomo_core.h"

namespace otomo {

const float SOUR = -0.3f, HABIT_SHOWS = 0.25f;
const int V_ACTIONS = V_HUNTER + 6, V_LARGE = V_ACTIONS + N_HACT, V_SMALL = V_LARGE + 3;
static const char *MON_N[4] = {"large", "small", "zone", "none"};
static const char *HP_N[2] = {"low", "ok"};
static const char *WHO_N[5] = {"fights", "rests", "moves", "works", "other"};
static const char *STYLE_N[7] = {"calm", "attacks", "gets_hit", "heals", "gathers", "crouches", "runs"};

struct Ctx {
    uint8_t mon, hp, who, style;
    bool operator==(const Ctx &o) const { return mon == o.mon && hp == o.hp && who == o.who && style == o.style; }
    int print(char *out, size_t n) const { return snprintf(out, n, "%s|%s|%s|%s", MON_N[mon], HP_N[hp], WHO_N[who], STYLE_N[style]); }
};

// habits.context: the situation in four words, from the (rounded) senses and the owner's style shares
inline Ctx context(const float *v, const float *style) {
    Ctx c;
    bool large = v[V_LARGE] > 0 && v[V_LARGE + 1] < 0.3f, small = v[V_SMALL] > 0 && v[V_SMALL + 1] < 0.3f;
    c.mon = large ? 0 : small ? 1 : (v[V_LARGE] || v[V_SMALL]) ? 2 : 3;
    c.hp = v[0] < 0.5f ? 0 : 1;
    int act = 0;
    for (int k = 1; k < N_HACT; k++) if (v[V_ACTIONS + k] > v[V_ACTIONS + act]) act = k;
    switch (act) {
        case A_WEAPON: case A_DRAWN: case A_HIT: c.who = 0; break;
        case A_IDLE: case A_CROUCH: case A_BED: case A_EAT_DRINK: case A_BBQ: case A_FISH: case A_STAND_UP: c.who = 1; break;
        case A_WALK: case A_RUN: case A_CROUCH_WALK: case A_CRAWL: case A_BUMP: c.who = 2; break;
        case A_GATHER: case A_CARVE: c.who = 3; break;
        default: c.who = 4;
    }
    int best = 0;
    for (int k = 1; k < N_STYLE; k++) if (style[k] > style[best]) best = k;
    c.style = style[best] >= 0.15f ? best + 1 : 0;
    return c;
}

// the probabilities of the answers to p for inputs x (the brain as it is now): neuro._dist
inline void probs_x(const Brain &B, const float *x, int p, const uint8_t *opts, int n, float temp, float *out) {
    float h[N_HID];
    for (int j = 0; j < N_HID; j++) {
        float a = B.b1[j];
        for (int i = 0; i < N_IN; i++) a += B.W1[j][i] * x[i];
        h[j] = tanhf(a);
    }
    float other = logf((1.f - GAME_SHARE) / (float)(n > 1 ? n - 1 : 1)), mine = logf(GAME_SHARE), mx = -1e30f;
    for (int k = 0; k < n; k++) {
        int a = opts[k];
        float z = B.b2[a];
        for (int j = 0; j < N_HID; j++) z += B.W2[a][j] * h[j];
        z = z > CAP ? CAP : (z < -CAP ? -CAP : z);
        out[k] = ((a == p ? mine : other) + z) / temp;
        if (out[k] > mx) mx = out[k];
    }
    float sum = 0, mn = 1;
    for (int k = 0; k < n; k++) { out[k] = expf(out[k] - mx); sum += out[k]; }
    for (int k = 0; k < n; k++) { out[k] /= sum; if (out[k] < mn) mn = out[k]; }
    if (mn < FLOOR_P) { sum = 0; for (int k = 0; k < n; k++) { if (out[k] < FLOOR_P) out[k] = FLOOR_P; sum += out[k]; } for (int k = 0; k < n; k++) out[k] /= sum; }
}

struct DiaryLine { uint32_t step; uint8_t kind, proposal, answer; Ctx ctx; float value; };   // kind 0 sour 1 habit 2 faded
struct Habit { Ctx ctx; uint8_t proposal, answer; float probability, day_one; };

struct Diary {
    static const int SEEN = 40, LINES = 40, SHOWN = 64;
    // a situation's inputs kept as int16 thousandths (10 KB instead of 21: the C3's heap is what limits it)
    struct Seen { Ctx ctx; uint8_t p; uint32_t age; int16_t x[N_IN]; } seen[SEEN]; int n_seen = 0; uint32_t clock = 0;
    static void unpack(const Seen &s, float *x) { for (int i = 0; i < N_IN; i++) x[i] = s.x[i] / 1000.f; }
    DiaryLine lines[LINES]; int n_lines = 0, head = 0;
    struct Shown { Ctx ctx; uint8_t p, a; } shown[SHOWN]; int n_shown = 0;
    bool dirty = false;

    void clear() { n_seen = 0; clock = 0; n_lines = 0; head = 0; n_shown = 0; dirty = true; }   // a new cat: nothing lived
    void add(const DiaryLine &l) {
        lines[(head + n_lines) % LINES] = l;
        if (n_lines < LINES) n_lines++; else head = (head + 1) % LINES;
        dirty = true;
    }
    int find_shown(const Ctx &c, int p, int a) const {
        for (int k = 0; k < n_shown; k++) if (shown[k].ctx == c && shown[k].p == p && shown[k].a == a) return k;
        return -1;
    }
    // neuro._remember + _note, after a lesson it lived (x = its inputs, f = the feeling)
    void remember(const Brain &B, const float *x, int p, int a, float f, const Cat &nat, uint32_t step) {
        Ctx c = context(x, x + VECTOR_LEN + N_BEH);
        int slot = -1, oldest = 0;
        for (int k = 0; k < n_seen; k++) {
            if (seen[k].ctx == c && seen[k].p == p) slot = k;
            if (seen[k].age < seen[oldest].age) oldest = k;
        }
        if (slot < 0) slot = n_seen < SEEN ? n_seen++ : oldest;
        seen[slot].ctx = c; seen[slot].p = (uint8_t)p; seen[slot].age = ++clock;
        for (int i = 0; i < N_IN; i++) { float v = x[i] * 1000.f; seen[slot].x[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lroundf(v)); }
        dirty = true;
        if (f <= SOUR) add({step, 0, (uint8_t)p, (uint8_t)a, c, f});
        if (a == p) return;
        uint8_t opts[N_BEH]; float pr[N_BEH];
        int n = allowed(p, nat, opts);
        probs_x(B, x, p, opts, n, 0.7f + 0.6f * (nat.weights[1] / 100.f), pr);
        float now = 0;
        for (int k = 0; k < n; k++) if (opts[k] == a) now = pr[k];
        int k = find_shown(c, p, a);
        if (now >= HABIT_SHOWS && k < 0) {
            if (n_shown < SHOWN) shown[n_shown++] = {c, (uint8_t)p, (uint8_t)a};
            add({step, 1, (uint8_t)p, (uint8_t)a, c, now});
        } else if (now < HABIT_SHOWS * 0.5f && k >= 0) {
            shown[k] = shown[--n_shown];
            add({step, 2, (uint8_t)p, (uint8_t)a, c, now});
        }
    }
    // neuro.strongest: the answers that moved most from day one, in the situations it lived
    int strongest(const Brain &B, const Cat &nat, Habit *out, int max) const {
        int m = 0;                                        // the top `max` by (probability - day one), kept sorted
        float temp = 0.7f + 0.6f * (nat.weights[1] / 100.f);
        for (int s = 0; s < n_seen; s++) {
            int p = seen[s].p;
            uint8_t opts[N_BEH]; float pr[N_BEH];
            int n = allowed(p, nat, opts);
            if (n < 2) continue;
            float x[N_IN];
            unpack(seen[s], x);
            probs_x(B, x, p, opts, n, temp, pr);
            float day[N_BEH], zs = 0, other = (1.f - GAME_SHARE) / (n - 1);
            for (int k = 0; k < n; k++) { day[k] = powf(opts[k] == p ? GAME_SHARE : other, 1.f / temp); zs += day[k]; }
            for (int k = 0; k < n; k++) {
                if (opts[k] == p || pr[k] <= day[k] / zs + 0.01f) continue;
                Habit hb = {seen[s].ctx, (uint8_t)p, opts[k], pr[k], day[k] / zs};
                float gain = hb.probability - hb.day_one;
                int at;
                if (m < max) at = m++;
                else if (gain > out[max - 1].probability - out[max - 1].day_one) at = max - 1;
                else continue;
                out[at] = hb;
                while (at > 0 && out[at].probability - out[at].day_one > out[at - 1].probability - out[at - 1].day_one) {
                    Habit t = out[at]; out[at] = out[at - 1]; out[at - 1] = t; at--;
                }
            }
        }
        return m;
    }
};

}  // namespace otomo
