// The cat LEARNS, in C++ (the C3's copy of habits.Life + neuro.Neuro.lived / learn_team + team.parts + praise.verdict +
// reward.outcome's 'entered'): a decision is lived SETTLE_S after it was taken, from what happened in the next minute,
// better or worse than USUAL for this cat with this owner. Checked against Python by the lab's mind_parity.py (learn).
#pragma once
#include "otomo_core.h"

namespace otomo {

const float LR = 0.06f, FEELING = 2.0f, FORGET = 0.0005f, HIDDEN_LR = 0.3f, LR_V = 0.05f, DEV_EMA = 0.02f;
const double HORIZON_S = 60.0, HALF_LIFE_S = 20.0, SETTLE_S = HORIZON_S + 2.0, ENTER_S = 1.5, PRAISE_S = 8.0;
const float PRAISE_WEIGHT = 0.8f;
const int V_CAT_HP = 0, V_POISONED = V_HUNTER + 3, V_ATTACK_UP = V_HUNTER + 4, V_DEFENCE_UP = V_HUNTER + 5;
enum Part { P_PROGRESS, P_SAFETY, P_PROVISIONS, P_TOGETHER, P_COMFORT, N_PARTS };

// everything that LEARNS (brain.bin v2): the network, its birth weights (forgetting pulls back to them), the value head
struct LearningBrain {
    Brain net;
    float W1_0[N_HID][N_IN], b1_0[N_HID];
    float Wv[N_HID], bv, dev;
    float tastes[N_PARTS];                                // progress, safety, provisions, together, comfort
    uint32_t step;
};

struct Row {                                              // one ATTACHED frame, what learning reads of it
    double t;
    uint8_t behaviour, gesture, hact, caught;             // gesture 0xFF = none; caught = monsters newly trapped
    uint16_t anim;
    float cat_hp, h_same, h_dist, h_hp, poisoned, att_up, def_up;   // from the rounded vector, like the Python rows
};
struct Pending { uint32_t row; uint8_t proposal, answer; float x[N_IN]; };
struct HitT { double t; uint8_t who; };
struct Lesson {
    uint32_t step; uint8_t proposal, answer; bool entered; float team, usual, feeling, praise;
    const float *x; bool imitated;                        // for the diary: its inputs (valid in the callback only)
};

template <int ROWS = 400, int PEND = 48, int HITS = 256>   // 62 s at 5 Hz = 310 rows; 48 pending: the busiest test 25+
struct Life {
    LearningBrain *b = nullptr;
    Row rows[ROWS]; uint32_t n_rows = 0;                  // ring: row k at rows[k % ROWS]
    Pending pend[PEND]; int p_head = 0, p_n = 0;
    HitT hits[HITS]; uint32_t n_hits = 0;
    struct Key { uint16_t key; bool trapped; } prev_mon[MAX_MONSTERS]; int n_prev = 0;
    uint32_t lived_n = 0, dropped = 0;                   // dropped: decisions with no room left (never seen in a quest)
    Lesson last{};
    Lesson last_felt{}; bool felt = false;                // the latest lesson it FELT (entered): the page shows it
    uint16_t changed[256]; int n_changed = 0;             // the answer links the last lesson moved (answer << 8 | neuron)

    const Row &row(uint32_t k) const { return rows[k % ROWS]; }
    static bool trapped(uint8_t type, uint8_t id) { return type == 4 && id == 7; }   // senses.TRAPPED_ACTS
    static double w(double dt) { return pow(0.5, dt / HALF_LIFE_S); }

    void hit(double t, uint8_t who) { if (who <= 1) hits[n_hits++ % HITS] = {t, who}; }   // cat / hunter only

    // one attached frame: vr = the ROUNDED vector, style / profile = AFTER this frame (as Python's row and Life)
    template <typename OnLesson>
    void sample(const Sense &s, const State &st, const float vr[VECTOR_LEN], const float style[N_STYLE],
                const float profile[N_PROF], const uint8_t in_effect[TABLE_SIZE], double t, OnLesson on_lesson) {
        Row r;
        r.t = t; r.behaviour = s.cat.behaviour; r.anim = s.cat.anim; r.hact = (uint8_t)st.hunter_act;
        r.gesture = s.hunter.present && s.hunter.act_type == 6 ? s.hunter.act_id : 0xFF;
        r.cat_hp = vr[V_CAT_HP]; r.h_same = vr[V_HUNTER]; r.h_dist = vr[V_HUNTER + 1]; r.h_hp = vr[V_HUNTER_HP];
        r.poisoned = vr[V_POISONED]; r.att_up = vr[V_ATTACK_UP]; r.def_up = vr[V_DEFENCE_UP];
        r.caught = 0;                                     // team.parts: a monster entering the trapped action
        for (int i = 0; i < s.n_mon; i++) {
            if (!trapped(s.mon[i].act_type, s.mon[i].act_id)) continue;
            bool before = false;
            for (int j = 0; j < n_prev; j++) if (prev_mon[j].key == s.mon[i].key) { before = prev_mon[j].trapped; break; }
            if (!before) r.caught++;
        }
        n_prev = s.n_mon;
        for (int i = 0; i < s.n_mon; i++) prev_mon[i] = {s.mon[i].key, trapped(s.mon[i].act_type, s.mon[i].act_id)};
        uint32_t me = n_rows++;
        rows[me % ROWS] = r;
        for (int i = 0; i < s.n_prop; i++) {              // reward.own: not those under an expired lease
            if (s.prop[i] & 0x80) continue;
            uint8_t p = s.prop[i] & 0x7F;
            if (p >= N_BEH) continue;
            if (p_n == PEND) { dropped++; continue; }
            Pending &d = pend[(p_head + p_n++) % PEND];
            d.row = me; d.proposal = p; d.answer = in_effect[p] != EMPTY ? in_effect[p] : p;
            memset(d.x, 0, sizeof d.x);
            memcpy(d.x, vr, sizeof(float) * VECTOR_LEN);
            d.x[VECTOR_LEN + p] = 1.f;
            memcpy(d.x + VECTOR_LEN + N_BEH, style, sizeof(float) * N_STYLE);
            memcpy(d.x + VECTOR_LEN + N_BEH + N_STYLE, profile, sizeof(float) * N_PROF);
        }
        while (p_n > 0 && t - row(pend[p_head].row).t > SETTLE_S) {   // lived once its minute is over
            Pending &d = pend[p_head];
            lived(d, s.cat, on_lesson);
            p_head = (p_head + 1) % PEND; p_n--;
        }
    }
    void drop_pending() { p_n = 0; }                       // detached: decisions cut short are not lived

    // the quest is over (habits.Life.close): what is still pending is lived now, with the rows there are
    template <typename OnLesson> void close(const Cat &nat, OnLesson on_lesson) {
        while (p_n > 0) {
            lived(pend[p_head], nat, on_lesson);
            p_head = (p_head + 1) % PEND; p_n--;
        }
    }

    // ---- the outcome of decision d (team.parts, reward.outcome 'entered', praise.verdict)
    bool entered(const Pending &d) const {
        double t0 = row(d.row).t;
        for (uint32_t k = d.row; k < n_rows && row(k).t - t0 <= ENTER_S; k++) if (row(k).behaviour == d.answer) return true;
        return false;
    }
    int verdict(const Pending &d) const {                 // +1 Praise / Nod Head / Cheer, -1 Shake Hd / Frustrtd
        double t0 = row(d.row).t;
        uint8_t prev = row(d.row).gesture;
        for (uint32_t k = d.row + 1; k < n_rows && k < d.row + 400; k++) {
            const Row &r = row(k);
            if (r.t - t0 > PRAISE_S) break;
            if (r.gesture != 0xFF && r.gesture != prev) {
                if (r.gesture == 8 || r.gesture == 14 || r.gesture == 7) return 1;
                if (r.gesture == 13 || r.gesture == 10) return -1;
            }
            prev = r.gesture;
        }
        return 0;
    }
    void parts(const Pending &d, double out[N_PARTS]) const {
        double t0 = row(d.row).t;
        uint32_t end = d.row;
        while (end < n_rows && end < d.row + 2000 && row(end).t - t0 <= HORIZON_S) end++;
        for (int k = 0; k < N_PARTS; k++) out[k] = 0;
        if (end - d.row < 2) return;
        double wsum = 0;
        for (uint32_t k = d.row; k < end; k++) wsum += w(row(k).t - t0);
        double progress = 0;
        for (uint32_t k = 0; k < (n_hits < HITS ? n_hits : HITS); k++) {
            const HitT &h = hits[k];
            if (h.t - t0 >= 0 && h.t - t0 <= HORIZON_S) progress += w(h.t - t0);
        }
        progress /= 10;
        double safety = 0, provisions = 0, together = 0, comfort = 0;
        for (uint32_t k = d.row; k + 1 < end; k++) {
            const Row &a = row(k), &b2 = row(k + 1);
            double wb = w(b2.t - t0);
            safety -= wb * (fmax(0.0, (double)a.h_hp - b2.h_hp) + fmax(0.0, (double)a.cat_hp - b2.cat_hp));
            if (b2.behaviour == 45 && a.behaviour != 45) safety -= wb * 1.0;          // the cat knocked out
            if (b2.h_hp <= 0 && 0 < a.h_hp) safety -= wb * 2.0;                       // the owner down
            if (b2.anim == 1110 && a.anim != 1110) provisions += wb * 0.3;            // a gathering pick
            if ((a.behaviour == 4 || a.behaviour == 5) && a.hact != A_EAT_DRINK) provisions += wb * fmax(0.0, (double)b2.h_hp - a.h_hp);
            if ((a.behaviour == 6 || a.behaviour == 7) && (b2.att_up > a.att_up || b2.def_up > a.def_up)) provisions += wb * 0.2;
            if (a.behaviour == 9 && a.poisoned > 0 && b2.poisoned == 0) provisions += wb * 0.2;
            progress += wb * 0.5 * b2.caught;
        }
        for (uint32_t k = d.row; k < end; k++) {
            const Row &r = row(k);
            double wr = w(r.t - t0);
            together -= wr * (r.h_same <= 0 ? 1.0 : (double)r.h_dist) / wsum;
            bool combat = r.behaviour == 15 || r.behaviour == 16 || (r.behaviour >= 25 && r.behaviour <= 28);
            comfort -= wr * (combat ? 1.0 : 0.0) / wsum;
        }
        // Python rounds each part to 4 decimals before weighing it
        out[P_PROGRESS] = round(progress * 1e4) / 1e4; out[P_SAFETY] = round(safety * 1e4) / 1e4;
        out[P_PROVISIONS] = round(provisions * 1e4) / 1e4; out[P_TOGETHER] = round(together * 1e4) / 1e4;
        out[P_COMFORT] = round(comfort * 1e4) / 1e4;
    }

    // ---- the lesson (neuro.Neuro.learn_team)
    template <typename OnLesson> void lived(const Pending &d, const Cat &nat, OnLesson on_lesson) {
        lived_n++;
        LearningBrain &B = *b;
        bool in = entered(d);
        float praise = PRAISE_WEIGHT * verdict(d);
        double pt[N_PARTS];
        parts(d, pt);
        double team = 0;
        for (int k = 0; k < N_PARTS; k++) team += B.tastes[k] * pt[k];
        forget();
        last = {B.step, d.proposal, d.answer, in, (float)team, 0, 0, praise};
        if (in) {
            float h[N_HID];
            for (int j = 0; j < N_HID; j++) {
                float a = B.net.b1[j];
                for (int i = 0; i < N_IN; i++) a += B.net.W1[j][i] * d.x[i];
                h[j] = tanhf(a);
            }
            float usual = B.bv;
            for (int j = 0; j < N_HID; j++) usual += B.Wv[j] * h[j];
            float surprise = (float)team - usual;
            for (int j = 0; j < N_HID; j++) B.Wv[j] += LR_V * surprise * h[j];
            B.bv += LR_V * surprise;
            B.dev += DEV_EMA * (fabsf(surprise) - B.dev);
            float f = tanhf(surprise / fmaxf(0.05f, 2 * B.dev)) + praise;
            f = f > 1 ? 1 : (f < -1 ? -1 : f);
            last.usual = usual; last.feeling = f;
            last_felt = last; felt = true;
            last.x = d.x;
            last.imitated = imitate(d, f, nat);
        }
        on_lesson(last);
    }
    void forget() {
        LearningBrain &B = *b;
        B.step++;
        const float decay = 1 - FORGET;
        for (int a = 0; a < N_OUT; a++) { for (int j = 0; j < N_HID; j++) B.net.W2[a][j] *= decay; B.net.b2[a] *= decay; }
        for (int j = 0; j < N_HID; j++) {
            for (int i = 0; i < N_IN; i++) B.net.W1[j][i] = B.W1_0[j][i] + (B.net.W1[j][i] - B.W1_0[j][i]) * decay;
            B.net.b1[j] = B.b1_0[j] + (B.net.b1[j] - B.b1_0[j]) * decay;
        }
    }
    bool imitate(const Pending &d, float feeling, const Cat &nat) {   // self-imitation, weight 1 + FEELING * feeling
        LearningBrain &B = *b;
        uint8_t opts[N_BEH];
        int n = allowed(d.proposal, nat, opts), at = -1;
        for (int k = 0; k < n; k++) if (opts[k] == d.answer) at = k;
        if (at < 0) return false;
        float h[N_HID], z[N_BEH], p[N_BEH];
        for (int j = 0; j < N_HID; j++) {
            float a = B.net.b1[j];
            for (int i = 0; i < N_IN; i++) a += B.net.W1[j][i] * d.x[i];
            h[j] = tanhf(a);
        }
        float temp = 0.7f + 0.6f * (nat.weights[1] / 100.f), mine = logf(GAME_SHARE);
        float other = logf((1.f - GAME_SHARE) / (float)(n > 1 ? n - 1 : 1)), mx = -1e30f;
        for (int k = 0; k < n; k++) {
            int a = opts[k];
            float zz = B.net.b2[a];
            for (int j = 0; j < N_HID; j++) zz += B.net.W2[a][j] * h[j];
            z[k] = zz;
            float zc = zz > CAP ? CAP : (zz < -CAP ? -CAP : zz);
            p[k] = ((a == d.proposal ? mine : other) + zc) / temp;
            if (p[k] > mx) mx = p[k];
        }
        float sum = 0, mn = 1;
        for (int k = 0; k < n; k++) { p[k] = expf(p[k] - mx); sum += p[k]; }
        for (int k = 0; k < n; k++) { p[k] /= sum; if (p[k] < mn) mn = p[k]; }
        if (mn < FLOOR_P) { sum = 0; for (int k = 0; k < n; k++) { if (p[k] < FLOOR_P) p[k] = FLOOR_P; sum += p[k]; } for (int k = 0; k < n; k++) p[k] /= sum; }
        float wt = 1 + FEELING * feeling, gz[N_BEH] = {0};
        for (int k = 0; k < n; k++) gz[opts[k]] = fabsf(z[k]) < CAP ? ((k == at ? 1.f : 0.f) - p[k]) / temp * wt : 0.f;
        float gh[N_HID];
        for (int j = 0; j < N_HID; j++) {                 // with W2 before its update
            float s2 = 0;
            for (int k = 0; k < n; k++) s2 += B.net.W2[opts[k]][j] * gz[opts[k]];
            gh[j] = s2 * (1 - h[j] * h[j]);
        }
        n_changed = 0;
        for (int k = 0; k < n; k++) {
            int a = opts[k];
            for (int j = 0; j < N_HID; j++) {
                float dw = LR * gz[a] * h[j];
                B.net.W2[a][j] += dw;
                if (fabsf(dw) > 1e-4f && n_changed < 256) changed[n_changed++] = (uint16_t)(a << 8 | j);   // the page lights them
            }
            B.net.b2[a] += LR * gz[a];
        }
        for (int j = 0; j < N_HID; j++) {
            float g = HIDDEN_LR * LR * gh[j];
            for (int i = 0; i < N_IN; i++) B.net.W1[j][i] += g * d.x[i];
            B.net.b1[j] += g;
        }
        return true;
    }
};

}  // namespace otomo
