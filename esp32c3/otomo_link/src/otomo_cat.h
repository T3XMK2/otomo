// THE CAT on the C3: one frame in, one table out, in the same order as the Python mind (the lab's mind.py):
//   senses -> the table (with the owner as seen BEFORE this frame) -> his hits -> the owner profile and style updated
//   -> learning (only while attached). Used by main.cpp and by the native parity checks, so there is one copy.
#pragma once
#include "otomo_learn.h"

namespace otomo {

struct Decision {                                         // the page's timeline / decision tile (publish_status)
    double t; uint8_t proposed, chose; bool by_brain; uint8_t n; uint8_t opts[8]; float probs[8];
};

struct TheCat {
    LearningBrain lb;
    Mind mind;
    Life<> life;
    uint8_t last_table[TABLE_SIZE];
    bool attached = false;
    // for the page: the latest decisions, the proposal the 3D view thinks about, the latest frame's cat, attached time
    Decision feed[15]; int feed_n = 0, feed_head = 0;
    uint8_t view_proposal = 17;
    Cat last_cat{};
    double attached_s = 0, last_t = -1;
    bool has_frame = false;

    void note_decisions(const Sense &s) {
        for (int i = 0; i < s.n_prop; i++) {
            uint8_t p = s.prop[i] & 0x7F;
            if (p >= N_BEH) continue;
            bool lost = s.prop[i] & 0x80, brain = attached && !lost;
            Decision &d = feed[(feed_head + feed_n) % 15];
            if (feed_n < 15) feed_n++; else feed_head = (feed_head + 1) % 15;
            d.t = s.body_ms / 1000.0; d.proposed = p; d.by_brain = brain;
            d.chose = brain && last_table[p] != EMPTY ? last_table[p] : p;
            uint8_t opts[N_BEH]; float pr[N_BEH];
            int n = allowed(p, s.cat, opts);
            d.n = 0;
            if (brain && n > 1 && n <= 8) { mind.probs(p, opts, n, pr); d.n = n; for (int k = 0; k < n; k++) { d.opts[k] = opts[k]; d.probs[k] = pr[k]; } }
            if ((p >= 1 && p <= 9) || p == 15 || p == 16 || p == 17 || (p >= 25 && p <= 29)) view_proposal = p;
        }
    }

    void begin() { mind.brain = &lb.net; life.b = &lb; reset_owner(); }
    void reset_owner() {                                  // a new quest / a new body: the owner seen afresh
        mind.style.clear(); mind.profile.clear();       // in place: they are fixed rings (~21 KB)
        life.n_rows = 0; life.p_n = 0; life.p_head = 0; life.n_hits = 0; life.n_prev = 0;
        memset(last_table, EMPTY, TABLE_SIZE);
    }
    void detach() { attached = false; life.drop_pending(); }

    // team.drift: after a quest played attached the tastes wander a little (log space, step 0.03, within 0.1..5)
    template <typename Uniform> void drift_tastes(Uniform u) {
        for (int k = 0; k < N_PARTS; k++) {
            double u1 = u(), u2 = u();
            if (u1 < 1e-12) u1 = 1e-12;
            double g = sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
            double v = lb.tastes[k] * exp(g * 0.03);
            v = v < 0.1 ? 0.1 : (v > 5.0 ? 5.0 : v);
            lb.tastes[k] = (float)(round(v * 1e4) / 1e4);
        }
    }

    // One frame in two halves, so the table can leave BEFORE the slow part (learning can take ~250 ms on the C3):
    //   decide(): the senses and the table (the owner as seen before this frame)
    //   live():   his hits, the owner profile and style, learning - then the table becomes the one in effect
    // frame() = both, in the order of the Python mind. draw(): uniform [0, 1); on_lesson(const Lesson &) per lesson.
    State st;
    float v[VECTOR_LEN];
    uint8_t pending_table[TABLE_SIZE];

    template <typename Draw> void decide(const Sense &s, Draw draw, uint8_t table[TABLE_SIZE]) {
        state_from_sense(s, st);
        float sty[N_STYLE], pf[N_PROF];
        vector(st, v);
        mind.style.style(sty);
        mind.profile.features(pf);
        mind.prepare(v, sty, pf, s.cat);
        if (attached) mind.table(s.cat, draw, table);
        else memset(table, EMPTY, TABLE_SIZE);
        memcpy(pending_table, table, TABLE_SIZE);
        note_decisions(s);                                // (only for the page: it changes nothing above)
        double t = s.body_ms / 1000.0;
        if (attached && last_t >= 0 && t > last_t && t - last_t < 5) attached_s += t - last_t;
        last_t = t; last_cat = s.cat; has_frame = true;
    }
    template <typename Draw, typename OnLesson>
    void frame(const Sense &s, Draw draw, OnLesson on_lesson, uint8_t table[TABLE_SIZE]) {
        decide(s, draw, table);
        live(s, on_lesson);
    }
    template <typename OnLesson> void live(const Sense &s, OnLesson on_lesson) {
        float vr[VECTOR_LEN];
        double t = s.body_ms / 1000.0;
        for (int i = 0; i < s.n_hit; i++) {
            if (s.hit[i].who == 1) mind.profile.hit(s.body_ms);
            life.hit(t, s.hit[i].who);
        }
        for (int i = 0; i < VECTOR_LEN; i++) vr[i] = round4(v[i]);
        mind.profile.update(s.body_ms, vr, st.hunter_act);
        if (attached) {
            float pf2[N_PROF], sty2[N_STYLE];
            mind.profile.features(pf2);
            mind.style.observe(s.body_ms, st.hunter_act);
            mind.style.style_rounded(sty2);
            life.sample(s, st, vr, sty2, pf2, last_table, t, on_lesson);
        }
        memcpy(last_table, pending_table, TABLE_SIZE);
    }
};

// a cat BORN here (neuro.Neuro.__init__ + team.tastes): W1 ~ normal(0, 1/sqrt(129)), everything else zero, 'usual'
// deviation 0.1, and its own tastes around its personality (card weights; 255 = never attacks). Its own random draws:
// a cat born on the C3 is not the PC's cat of the same name, it is a new one. u() = uniform in (0, 1).
template <typename Uniform> void newborn(LearningBrain &b, const uint8_t w[4], Uniform u) {
    auto gauss = [&u]() {                                 // Box-Muller
        double u1 = u(), u2 = u();
        if (u1 < 1e-12) u1 = 1e-12;
        return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
    };
    const float sd = 1.f / sqrtf((float)N_IN);
    for (int j = 0; j < N_HID; j++) {
        for (int i = 0; i < N_IN; i++) b.W1_0[j][i] = b.net.W1[j][i] = (float)gauss() * sd;
        b.net.b1[j] = b.b1_0[j] = 0; b.Wv[j] = 0;
    }
    memset(b.net.W2, 0, sizeof b.net.W2); memset(b.net.b2, 0, sizeof b.net.b2);
    b.bv = 0; b.dev = 0.1f; b.step = 0;
    double weapon = w[0] == 255 ? 0.5 : w[0] / 100.0, bold = w[1] / 100.0;
    const double prior[N_PARTS] = {0.6 + 0.8 * weapon * (0.5 + bold), 1.6 - bold, 1.0, 1.0, 0.4};
    for (int k = 0; k < N_PARTS; k++) {
        double v = prior[k] * exp(gauss() * 0.5);
        v = v < 0.1 ? 0.1 : (v > 5.0 ? 5.0 : v);
        b.tastes[k] = (float)(round(v * 1e4) / 1e4);
    }
}

// brain.bin v2 <-> LearningBrain (the lab's brainfile.py)
inline bool load_v2(const uint8_t *d, size_t n, LearningBrain &b) {
    const size_t want = 4 + 4 * ((size_t)N_HID * N_IN * 2 + N_HID * 2 + (size_t)N_OUT * N_HID + N_OUT + N_HID + 2 + N_PARTS) + 4;
    if (n != want || memcmp(d, "FYB2", 4) != 0) return false;
    const uint8_t *p = d + 4;
    auto take = [&](void *dst, size_t bytes) { memcpy(dst, p, bytes); p += bytes; };
    take(b.net.W1, sizeof b.net.W1); take(b.net.b1, sizeof b.net.b1); take(b.W1_0, sizeof b.W1_0); take(b.b1_0, sizeof b.b1_0);
    take(b.net.W2, sizeof b.net.W2); take(b.net.b2, sizeof b.net.b2); take(b.Wv, sizeof b.Wv);
    take(&b.bv, 4); take(&b.dev, 4); take(b.tastes, sizeof b.tastes); take(&b.step, 4);
    return true;
}
inline size_t save_v2(const LearningBrain &b, uint8_t *d) {
    uint8_t *p = d;
    auto put = [&](const void *src, size_t bytes) { memcpy(p, src, bytes); p += bytes; };
    put("FYB2", 4);
    put(b.net.W1, sizeof b.net.W1); put(b.net.b1, sizeof b.net.b1); put(b.W1_0, sizeof b.W1_0); put(b.b1_0, sizeof b.b1_0);
    put(b.net.W2, sizeof b.net.W2); put(b.net.b2, sizeof b.net.b2); put(b.Wv, sizeof b.Wv);
    put(&b.bv, 4); put(&b.dev, 4); put(b.tastes, sizeof b.tastes); put(&b.step, 4);
    return p - d;
}

}  // namespace otomo
