// The studio page served BY THE C3 (backlog A8, owner 4 Oct: "la casa dovrebbe essere l'esp"): the same page and the
// same routes as the PC's server (the lab's studio.py), answered from the cat that lives here.
//   GET /                      the page (index.html.gz)          GET /api/meta      the game's lists (meta.json)
//   GET /api/cats              the one cat (ONE cat per home)   GET /api/cats/<id> its passport
//   GET /api/brain/<id>        its network: real strongest weights, the links the last lesson moved
//   GET /api/status            what it is doing now (the page polls it; there is no /api/stream here)
//   POST /api/attach           {attached}: the owner's button
//   POST /api/cats             a new brain: the comrade of the next quest gets it ({cancel: true} undoes it)
//                                                                POST /api/release   archive it in /released/
//   GET /api/released          the cats released from here        POST /api/return    one comes home again
//   POST /api/adopt            not on the C3 (it reads the game's save: the body will have to send it)
// The routes that change who lives here are in main.cpp (they own the brain); the views are here.
#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>

#include "otomo_cat.h"
#include "otomo_diary.h"

namespace otomo {

static const char *HACT_NAMES[N_HACT] = {"idle", "walk", "run", "bump", "crouch", "stand_up", "crouch_walk", "crawl",
                                         "bed", "gather", "carve", "eat_drink", "bbq", "fish", "drawn", "weapon", "hit",
                                         "other"};
static const char *PROF_NAMES[9] = {"attacks", "hits_landed", "hits_taken", "heals_at_hp", "low_hp", "gathers", "runs",
                                    "distance", "fights_alone"};
static const char *TASTE_NAMES[N_PARTS] = {"progress", "safety", "provisions", "together", "comfort"};

// what describe() and the checks need of /meta.json: small fixed tables (~2.5 KB; as a JsonDocument it held 7.6 KB of
// the heap). The 213 names are read from the file when a cat is created.
struct Meta {
    struct Pers { char temperament[16], attack[16]; int8_t weights[4]; };   // weights: -1 = never attacks
    struct Skill { char name[24]; bool swappable; };
    Pers pers[16]; Skill skills[48]; char coats[48][16];
    int n_pers = 0, n_skills = 0, n_coats = 0;
};

struct Home {                                             // what the web side needs besides the cat
    TheCat *cat;
    Diary *diary;
    bool *brain_ok;
    uint32_t *frames, *last_frame_ms;
    JsonDocument passport;                                // /passport.json
    Meta meta;                                            // the game's lists the C3 needs (/meta.json)
    Cat nature = Cat();                                   // its card's nature (studio: nature_of(passport)) - what
                                                          // its habits are read with, a frame or none since the boot
    String id, name;
    bool wrong_body = false;                              // the equipped game cat is not its body: no attach (5 Oct)
};

inline bool load_passport(Home &h) {
    File f = LittleFS.open("/passport.json", "r");
    if (!f) return false;
    DeserializationError e = deserializeJson(h.passport, f);
    f.close();
    if (e) return false;
    h.id = h.passport["id"].as<String>();
    h.name = h.passport["name"].as<String>();
    return true;
}

inline bool load_meta(Home &h) {
    File f = LittleFS.open("/meta.json", "r");
    if (!f) return false;
    JsonDocument filter, d;                               // (only while it is copied into the tables)
    filter["coats"] = true;
    filter["personalities"][0]["temperament"] = true;
    filter["personalities"][0]["attack"] = true;
    filter["personalities"][0]["weights"] = true;
    filter["skills"][0]["name"] = true;
    filter["skills"][0]["swappable"] = true;
    DeserializationError e = deserializeJson(d, f, DeserializationOption::Filter(filter));
    f.close();
    if (e) return false;
    Meta &m = h.meta;
    m.n_pers = m.n_skills = m.n_coats = 0;
    for (JsonVariant p : d["personalities"].as<JsonArray>()) {
        if (m.n_pers == 16) break;
        Meta::Pers &q = m.pers[m.n_pers++];
        strlcpy(q.temperament, p["temperament"] | "?", sizeof q.temperament);
        strlcpy(q.attack, p["attack"] | "?", sizeof q.attack);
        for (int k = 0; k < 4; k++) q.weights[k] = (int8_t)(p["weights"][k] | 50);
    }
    for (JsonVariant x : d["skills"].as<JsonArray>()) {
        if (m.n_skills == 48) break;
        Meta::Skill &q = m.skills[m.n_skills++];
        strlcpy(q.name, x["name"] | "?", sizeof q.name);
        q.swappable = x["swappable"] | false;
    }
    for (JsonVariant c : d["coats"].as<JsonArray>()) {
        if (m.n_coats == 48) break;
        strlcpy(m.coats[m.n_coats++], c | "?", sizeof m.coats[0]);
    }
    return true;
}

inline bool game_name(const char *name) {                // passport.game_names: one of the game's comrade names
    File f = LittleFS.open("/meta.json", "r");
    if (!f) return false;
    JsonDocument filter, d;
    filter["names"] = true;
    DeserializationError e = deserializeJson(d, f, DeserializationOption::Filter(filter));
    f.close();
    if (e) return false;
    for (JsonVariant n : d["names"].as<JsonArray>()) if (strcmp(n.as<const char *>(), name) == 0) return true;
    return false;
}

// the game's comrade name number k (meta.json names): the body's card carries the index only
inline bool game_name_at(int k, char *out, size_t n) {
    File f = LittleFS.open("/meta.json", "r");
    if (!f) return false;
    JsonDocument filter, d;
    filter["names"] = true;
    DeserializationError e = deserializeJson(d, f, DeserializationOption::Filter(filter));
    f.close();
    JsonArray names = d["names"];
    if (e || k < 0 || k >= (int)names.size()) return false;
    strlcpy(out, names[k] | "", n);
    return out[0] != 0;
}

// passport.Passport.describe, from the passport as it is NOW (the export's copy goes stale after a quest)
inline void describe(const Home &h, char *out, size_t n) {
    JsonVariantConst p = h.passport.as<JsonVariantConst>();
    const Meta &m = h.meta;
    int pi = p["personality"] | 0, coat = p["coat"].isNull() ? -1 : (p["coat"] | -1);
    size_t k = snprintf(out, n, "%s (%s)", p["name"] | "", p["id"] | "");
    if (coat >= 0 && k < n) k += snprintf(out + k, n - k, ", %s coat", coat < m.n_coats ? m.coats[coat] : "?");
    if (k < n) k += snprintf(out + k, n - k, ": %s / %s, ", pi < m.n_pers ? m.pers[pi].temperament : "?",
                             pi < m.n_pers ? m.pers[pi].attack : "?");
    JsonArrayConst sk = p["skills"];
    if (sk.size() == 0 && k < n) k += snprintf(out + k, n - k, "no skills");
    bool first = true;
    for (JsonVariantConst b : sk) {
        int i = b | -1;
        if (k < n) k += snprintf(out + k, n - k, "%s%s", first ? "" : ", ", i >= 0 && i < m.n_skills ? m.skills[i].name : "?");
        first = false;
    }
    if (k < n) snprintf(out + k, n - k, "; owner %s; %s; %d quests attached, %d decisions lived", p["owner"] | "",
                        p["origin"] | "new", p["growth"]["quests"] | 0, p["growth"]["decisions_lived"] | 0);
}

inline bool has_cat(const Home &h) { return h.id.length() > 0; }

// ------------------------------------------------------------------------- its BODY (passport.meet_body, 5 Oct 2026)
// The neural cat rides a real game cat (hired at the Comrade Board) whose level, stats, points, skills and training grow
// by the game's rules; the body sends that cat's hall card with every quest's HELLO. The first one met is adopted, any
// other refused (no attach), the growth kept when it changed (at most 40) - the same as the lab's passport.py.
const int BODY_HISTORY_MAX = 40;
const int S_LEVEL = 0x16, S_EXP = 0x26, S_HP = 0x28, S_FONDNESS = 0x2A, S_POINTS = 0x30, S_SKILLS = 0x34,
          S_LEARNED = 0x44, S_LEARNED_N = 0x47, S_ID = 0x68;                    // nature.py

inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

inline void body_snapshot(const uint8_t *c, uint8_t training, JsonObject o) {
    o["name_idx"] = c[1]; o["level"] = c[S_LEVEL] + 1; o["exp"] = c[S_EXP] | (c[S_EXP + 1] << 8);
    float atk, def;
    memcpy(&atk, c + 0x18, 4); memcpy(&def, c + 0x1C, 4);
    o["attack"] = round(atk * 10) / 10; o["defence"] = round(def * 10) / 10; o["hp"] = c[S_HP];
    o["fondness"] = (int16_t)(c[S_FONDNESS] | (c[S_FONDNESS + 1] << 8)); o["points"] = c[S_POINTS] | (c[S_POINTS + 1] << 8);
    JsonArray l = o["learned"].to<JsonArray>();
    for (int i = 0; i < 3 && i < c[S_LEARNED_N]; i++) if (c[S_LEARNED + i] != 0xFF) l.add(c[S_LEARNED + i]);
    JsonArray e = o["equipped"].to<JsonArray>();
    uint32_t lo = rd32(c + S_SKILLS), hi = rd32(c + S_SKILLS + 4);
    for (int b = 0; b < 64; b++) if (b < 32 ? (lo >> b) & 1 : (hi >> (b - 32)) & 1) e.add(b);
    o["training"] = training;
}

inline void now_text(char *out, size_t n) {
    strlcpy(out, "unknown", n);
    struct tm tm;
    if (getLocalTime(&tm, 10)) strftime(out, n, "%Y-%m-%d %H:%M", &tm);
}

// -> "adopted" / "same" / "other"; changes the passport (the caller saves it when it is not "other")
inline const char *meet_body(Home &h, const uint8_t *card, uint8_t training, bool &changed) {
    char id[17], when[20];
    for (int i = 0; i < 8; i++) snprintf(id + 2 * i, 3, "%02x", card[S_ID + i]);
    now_text(when, sizeof when);
    JsonDocument snap;
    body_snapshot(card, training, snap.to<JsonObject>());
    changed = false;
    JsonVariant body = h.passport["body"];
    JsonArray hist = h.passport["body_history"];
    if (hist.isNull()) hist = h.passport["body_history"].to<JsonArray>();
    if (body.isNull() || body["id"].isNull()) {
        JsonObject b = h.passport["body"].to<JsonObject>();
        b["id"] = id; b["name_idx"] = card[1]; b["adopted"] = when;
        JsonObject e = hist.add<JsonObject>();
        e["when"] = when;
        for (JsonPair kv : snap.as<JsonObject>()) e[kv.key()] = kv.value();
        changed = true;
        return "adopted";
    }
    if (strcmp(body["id"] | "", id) != 0) return "other";
    bool same = hist.size() > 0;
    if (same) {
        JsonObject last = hist[hist.size() - 1];
        for (JsonPair kv : snap.as<JsonObject>())
            if (last[kv.key()] != kv.value()) { same = false; break; }
    }
    if (same) return "same";
    JsonObject e = hist.add<JsonObject>();
    e["when"] = when;
    for (JsonPair kv : snap.as<JsonObject>()) e[kv.key()] = kv.value();
    while (hist.size() > BODY_HISTORY_MAX) hist.remove(0);
    changed = true;
    return "same";
}

inline void send_error(WebServer &web, int code, const String &msg) {
    JsonDocument d;
    d["error"] = msg;
    String s; serializeJson(d, s);
    web.send(code, "application/json", s);
}

// <id>-YYYYmmdd-HHMMSS: an archive's folder name (passport.release)
inline bool archive_name(const char *a) {
    size_t n = strlen(a);
    if (n < 18 || n > 32 + 16) return false;
    const char *t = a + n - 16;                           // "-YYYYmmdd-HHMMSS"
    if (t[0] != '-' || t[9] != '-') return false;
    for (int i = 1; i < 16; i++) if (i != 9 && !isdigit((unsigned char)t[i])) return false;
    if (!(islower((unsigned char)a[0]) || isdigit((unsigned char)a[0]))) return false;
    for (const char *c = a; c < t; c++)
        if (!(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '-' || *c == '_')) return false;
    return true;
}

inline bool save_passport(Home &h) {                      // atomic: a new file, then renamed over the old one
    File f = LittleFS.open("/passport.new", "w");
    if (!f) return false;
    bool ok = serializeJson(h.passport, f) > 0;
    f.close();
    return ok && LittleFS.rename("/passport.new", "/passport.json");
}

// passport.Passport.record_quest: a quest played ATTACHED is the only thing it grows from
inline void record_quest(Home &h, const char *quest, const char *run, double attached_s, uint32_t lived) {
    JsonObject g = h.passport["growth"];
    g["quests"] = (g["quests"] | 0) + 1;
    g["attached_s"] = round(((g["attached_s"] | 0.0) + attached_s) * 10) / 10;
    g["decisions_lived"] = (g["decisions_lived"] | 0) + lived;
    char when[20] = "unknown";
    struct tm tm;
    if (getLocalTime(&tm, 10)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);   // NTP (setup), else unknown
    JsonArray hist = h.passport["history"];
    if (hist.isNull()) hist = h.passport["history"].to<JsonArray>();
    JsonObject e = hist.add<JsonObject>();
    e["when"] = when; e["quest"] = quest; e["attached_s"] = round(attached_s * 10) / 10; e["lived"] = lived;
    e["owner"] = h.passport["owner"]; e["run"] = run; e["home"] = "C3";
    while (hist.size() > 40) hist.remove(0);              // the flash keeps the latest 40 quests
}

// 5 Oct: two pages left open polling the C3 (status every second, the brain at every lesson) took its free heap down
// to 5 KB and it stopped answering. A heavy answer is refused while the heap is short: a 503 is a few bytes, the page
// tries again (its own pace).
const uint32_t LOW_HEAP = 30000;                      // 24000 still let two pages + a quest reach 11 KB (live)
inline bool short_of_memory(WebServer &web) {
    if (ESP.getFreeHeap() >= LOW_HEAP) return false;
    web.sendHeader("Retry-After", "2");
    web.send(503, "application/json", "{\"error\":\"the C3 is short of memory: try again in a moment\"}");
    return true;
}

inline void send_file(WebServer &web, const char *path, const char *type, bool gz = false) {
    if (short_of_memory(web)) return;
    File f = LittleFS.open(path, "r");
    if (!f) { web.send(404, "application/json", "{\"error\":\"missing file\"}"); return; }
    (void)gz;                                             // streamFile adds Content-Encoding: gzip for *.gz itself
    web.streamFile(f, type);
    f.close();
}

// Answers are WRITTEN STRAIGHT into 512-byte chunks, no JSON document and no String in between (5 Oct: building them
// in memory took the C3 down to 5 KB, then 11 KB of free heap mid-quest): ChunkOut is a Print feeding the web server.
struct ChunkOut : public Print {
    WebServer &web; char buf[512]; size_t n = 0;
    explicit ChunkOut(WebServer &w) : web(w) {}
    size_t write(uint8_t c) override { buf[n++] = (char)c; if (n == sizeof buf) flush(); return 1; }
    void flush() { if (n) { web.sendContent(buf, n); n = 0; } }
    void num(float v, int digits = 3) {                      // JSON has no NaN / inf
        if (isnan(v) || isinf(v)) { print("0"); return; }
        printf("%.*f", digits, v);
    }
    void list(const float *v, int n2, int digits = 3) {
        print('[');
        for (int i = 0; i < n2; i++) { if (i) print(','); num(v[i], digits); }
        print(']');
    }
};

inline void begin_json(WebServer &web) {
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "application/json", "");
}
inline void end_json(WebServer &web, ChunkOut &out) { out.flush(); web.sendContent(""); }

// passport.released: newest first (the names end in the time, so the newest sort last)
inline void send_released(WebServer &web) {
    if (short_of_memory(web)) return;
    static const int MAX = 32;
    String names[MAX]; int n = 0;
    File dir = LittleFS.open("/released");
    if (dir && dir.isDirectory()) {
        for (File e = dir.openNextFile(); e && n < MAX; e = dir.openNextFile()) {
            if (e.isDirectory() && archive_name(e.name())) names[n++] = e.name();
            e.close();
        }
    }
    for (int i = 1; i < n; i++)                           // newest first: by the time at the end of the name
        for (int j = i; j > 0 && strcmp(names[j].c_str() + names[j].length() - 15, names[j - 1].c_str() + names[j - 1].length() - 15) > 0; j--)
            std::swap(names[j], names[j - 1]);
    begin_json(web);
    ChunkOut out(web);
    out.print("{\"released\":[");
    for (int i = 0; i < n; i++) {
        File f = LittleFS.open("/released/" + names[i] + "/passport.json", "r");
        JsonDocument filter, d;
        filter["id"] = true; filter["name"] = true; filter["growth"]["quests"] = true;
        if (f) { deserializeJson(d, f, DeserializationOption::Filter(filter)); f.close(); }
        const char *t = names[i].c_str() + names[i].length() - 15;   // YYYYmmdd-HHMMSS
        out.printf("%s{\"archive\":\"%s\",\"id\":\"%s\",\"name\":\"%s\",\"quests\":%d,"
                   "\"released\":\"%.4s-%.2s-%.2s %.2s:%.2s\"}", i ? "," : "", names[i].c_str(), d["id"] | "",
                   d["name"] | "", d["growth"]["quests"] | 0, t, t + 4, t + 6, t + 9, t + 11);
    }
    out.print("]}");
    end_json(web, out);
}

// the strongest 6 incoming weights per neuron (neuro.Neuro.structure, top_k = 6) as [input, neuron, weight]; W(row, col)
template <typename F> void top_edges(ChunkOut &out, int rows, int cols, F W) {
    bool first = true;
    out.print('[');
    for (int j = 0; j < rows; j++) {
        int best[6]; float bw[6]; int nb = 0;              // kept sorted by |w|, strongest first
        for (int i = 0; i < cols; i++) {
            float w = W(j, i);
            if (w == 0) continue;
            int k;
            if (nb < 6) k = nb++;
            else if (fabsf(w) > fabsf(bw[5])) k = 5;
            else continue;
            best[k] = i; bw[k] = w;
            while (k > 0 && fabsf(bw[k]) > fabsf(bw[k - 1])) { std::swap(bw[k], bw[k - 1]); std::swap(best[k], best[k - 1]); k--; }
        }
        for (int k = 0; k < nb; k++) {
            if (!first) out.print(',');
            first = false;
            out.printf("[%d,%d,", best[k], j); out.num(bw[k], 4); out.print(']');
        }
    }
    out.print(']');
}

inline void tastes_json(ChunkOut &out, const TheCat &c) {
    out.print('{');
    for (int k = 0; k < N_PARTS; k++) { if (k) out.print(','); out.printf("\"%s\":", TASTE_NAMES[k]); out.num(c.lb.tastes[k], 4); }
    out.print('}');
}
inline void feeling_json(ChunkOut &out, const TheCat &c) {
    if (!c.life.felt) { out.print("null"); return; }     // like Python's last_feeling: lessons it entered only
    const Lesson &l = c.life.last_felt;
    out.print("{\"outcome\":"); out.num(l.team); out.print(",\"usual\":"); out.num(l.usual);
    out.print(",\"feeling\":"); out.num(l.feeling); out.print(",\"praise\":"); out.num(l.praise);
    out.print('}');
}

inline void send_brain(WebServer &web, Home &h) {
    if (short_of_memory(web)) return;
    const TheCat &c = *h.cat;
    begin_json(web);
    ChunkOut out(web);
    out.print("{\"layers\":[{\"name\":\"inputs\",\"size\":129,\"names\":");
    File f = LittleFS.open("/names.json", "r");           // the 129 input names, as they are in the file
    if (f) { int ch; while ((ch = f.read()) >= 0) out.write((uint8_t)ch); f.close(); } else out.print("[]");
    out.print("},{\"name\":\"hidden\",\"size\":32},{\"name\":\"answers\",\"size\":46}],\"edges\":[");
    top_edges(out, N_HID, N_IN, [&c](int j, int i) { return c.lb.net.W1[j][i]; });
    out.print(',');
    top_edges(out, N_OUT, N_HID, [&c](int j, int i) { return c.lb.net.W2[j][i]; });
    out.printf("],\"step\":%lu,\"tastes\":", (unsigned long)c.lb.step);
    tastes_json(out, c);
    out.print(",\"dev\":"); out.num(c.lb.dev, 4);
    out.print(",\"last_feeling\":"); feeling_json(out, c);
    out.print(",\"changed\":[");
    for (int k = 0; k < c.life.n_changed; k++) out.printf("%s[%d,%d]", k ? "," : "", c.life.changed[k] >> 8, c.life.changed[k] & 0xFF);
    out.printf("],\"cat\":\"%s\",\"born\":false}", h.id.c_str());
    end_json(web, out);
}

inline void send_status(WebServer &web, Home &h) {
    if (short_of_memory(web)) return;
    TheCat &c = *h.cat;
    uint32_t age = millis() - *h.last_frame_ms;
    bool running = *h.frames > 0 && age < 3000;
    begin_json(web);
    ChunkOut out(web);
    out.printf("{\"instance\":0,\"running\":%s,\"age_s\":", running ? "true" : "false");
    out.num(*h.frames ? age / 1000.0f : -1, 1);
    out.printf(",\"wants_attached\":%s,\"live\":{\"passport\":\"%s\",\"cat\":\"%s\",\"attached\":%s,\"brain_alive\":%s,\"attached_s\":",
               c.attached ? "true" : "false", h.id.c_str(), h.name.c_str(), c.attached ? "true" : "false",
               *h.brain_ok ? "true" : "false");
    out.num((float)c.attached_s, 1);
    if (c.has_frame && *h.brain_ok) {                     // (no cat here: no brain to show)
        out.printf(",\"behaviour\":%u,\"hp\":", c.last_cat.behaviour);
        out.num(c.last_cat.hp_max > 0 ? (float)c.last_cat.hp / c.last_cat.hp_max : 0);
        out.printf(",\"hunter\":\"%s\",\"monsters\":%d", HACT_NAMES[c.st.hunter_act], c.st.n_mon);
        // the 3D view: the network thinking about the latest choice, with the owner as it sees him now
        int p = c.view_proposal;
        float sty[N_STYLE], pf[N_PROF], x[N_IN] = {0}, hh[N_HID], z[N_OUT], pr[N_BEH];
        c.mind.style.style(sty);
        c.mind.profile.features(pf);
        memcpy(x, c.v, sizeof(float) * VECTOR_LEN);
        x[VECTOR_LEN + p] = 1;
        memcpy(x + VECTOR_LEN + N_BEH, sty, sizeof sty);
        memcpy(x + VECTOR_LEN + N_BEH + N_STYLE, pf, sizeof pf);
        for (int j = 0; j < N_HID; j++) {
            float a = c.lb.net.b1[j];
            for (int i = 0; i < N_IN; i++) a += c.lb.net.W1[j][i] * x[i];
            hh[j] = tanhf(a);
        }
        for (int a = 0; a < N_OUT; a++) { float s2 = c.lb.net.b2[a]; for (int j = 0; j < N_HID; j++) s2 += c.lb.net.W2[a][j] * hh[j]; z[a] = s2; }
        uint8_t opts[N_BEH];
        int n = allowed(p, c.last_cat, opts);
        c.mind.prepare(c.v, sty, pf, c.last_cat);
        c.mind.probs(p, opts, n, pr);
        out.printf(",\"brain\":{\"proposal\":%d,\"step\":%lu,\"inputs\":", p, (unsigned long)c.lb.step);
        out.list(x, N_IN);
        out.print(",\"hidden\":"); out.list(hh, N_HID);
        out.print(",\"outputs\":"); out.list(z, N_OUT);
        out.print(",\"allowed\":[");
        for (int k = 0; k < n; k++) out.printf("%s%u", k ? "," : "", opts[k]);
        out.print("],\"probabilities\":{");
        for (int k = 0; k < n; k++) { out.printf("%s\"%u\":", k ? "," : "", opts[k]); out.num(pr[k]); }
        out.print("},\"tastes\":"); tastes_json(out, c);
        out.print(",\"dev\":"); out.num(c.lb.dev, 4);
        out.print(",\"last_feeling\":"); feeling_json(out, c);
        out.print(",\"owner\":{");
        for (int w = 0; w < 2; w++) for (int k = 0; k < 9; k++) {
            out.printf("%s\"owner %s (%s)\":", (w || k) ? "," : "", PROF_NAMES[k], w ? "180 s" : "30 s");
            out.num(pf[w * 9 + k], 4);
        }
        out.print("}}");
    }
    out.print(",\"decisions\":[");                         // newest first
    for (int k = c.feed_n - 1, first = 1; k >= 0; k--, first = 0) {
        const Decision &e = c.feed[(c.feed_head + k) % 15];
        out.printf("%s{\"t\":", first ? "" : ",");
        out.num((float)e.t, 1);
        out.printf(",\"proposed\":%u,\"chose\":%u,\"by\":\"%s\"", e.proposed, e.chose, e.by_brain ? "brain" : "game");
        if (e.n) {
            out.print(",\"probabilities\":{");
            for (int i = 0; i < e.n; i++) { out.printf("%s\"%u\":", i ? "," : "", e.opts[i]); out.num(e.probs[i]); }
            out.print('}');
        }
        out.print('}');
    }
    out.print("]}}");
    end_json(web, out);
}

// the cat's view (studio.cat_view): its passport, its strongest habits, the end of its diary
inline void send_cat_view(WebServer &web, Home &h) {
    if (short_of_memory(web)) return;
    static const char *KIND[3] = {"sour", "habit", "faded"};
    char text[200];
    describe(h, text, sizeof text);
    begin_json(web);
    ChunkOut out(web);
    out.print("{\"passport\":");
    serializeJson(h.passport, out);
    out.print(",\"describe\":");
    JsonDocument t;                                       // (quoted and escaped as JSON)
    t.set((const char *)text);
    serializeJson(t, out);
    out.printf(",\"brain\":true,\"habits\":{\"step\":%lu,\"strongest\":[", (unsigned long)h.cat->lb.step);
    Habit hb[8];
    int n = h.diary->strongest(h.cat->lb.net, h.nature, hb, 8);
    char ctx[48];
    for (int k = 0; k < n; k++) {
        hb[k].ctx.print(ctx, sizeof ctx);
        out.printf("%s{\"context\":\"%s\",\"proposal\":%u,\"answer\":%u,\"probability\":", k ? "," : "", ctx,
                   hb[k].proposal, hb[k].answer);
        out.num(hb[k].probability); out.print(",\"day_one\":"); out.num(hb[k].day_one); out.print('}');
    }
    out.print("]},\"diary\":[");                          // oldest first, like the Python diary
    const Diary &d = *h.diary;
    for (int k = 0; k < d.n_lines; k++) {
        const DiaryLine &l = d.lines[(d.head + k) % Diary::LINES];
        l.ctx.print(ctx, sizeof ctx);
        out.printf("%s{\"step\":%lu,\"kind\":\"%s\",\"context\":\"%s\",\"proposal\":%u,\"answer\":%u,\"%s\":",
                   k ? "," : "", (unsigned long)l.step, KIND[l.kind], ctx, l.proposal, l.answer,
                   l.kind == 0 ? "valence" : "probability");
        out.num(l.value); out.print('}');
    }
    out.print("]}");
    end_json(web, out);
}

inline void web_routes(WebServer &web, Home &h) {
    web.on("/", HTTP_GET, [&web] { send_file(web, "/index.html.gz", "text/html", true); });
    web.on("/api/meta", HTTP_GET, [&web] { send_file(web, "/meta.json", "application/json"); });
    web.on("/api/cats", HTTP_GET, [&web, &h] {          // studio: {cats, max_cats, room, full}
        JsonDocument d;
        JsonArray cats = d["cats"].to<JsonArray>();
        if (has_cat(h)) {
            char text[200];
            describe(h, text, sizeof text);
            JsonObject c = cats.add<JsonObject>();
            c["id"] = h.id; c["name"] = h.name; c["describe"] = text;
        }
        d["max_cats"] = 1; d["room"] = !has_cat(h);
        d["waiting"] = !has_cat(h) && LittleFS.exists("/waiting");   // a new brain waits for its body (the next quest)
        d["full"] = has_cat(h) ? String("One cat per home: ") + h.name + " lives here. Release it first (its passport and brain are archived)." : String("");
        String s; serializeJson(d, s); web.send(200, "application/json", s);
    });
    web.on("/api/released", HTTP_GET, [&web] { send_released(web); });
    web.on("/api/status", HTTP_GET, [&web, &h] { send_status(web, h); });
    web.on("/api/attach", HTTP_POST, [&web, &h] {
        JsonDocument b;
        deserializeJson(b, web.arg("plain"));
        bool want = b["attached"] | false;
        if (want && h.wrong_body)                          // the equipped game cat is not its body
            return send_error(web, 409, "the comrade equipped in the game is not " + h.name + "'s body: equip its own cat");
        if (!want && h.cat->attached) h.cat->detach();     // decisions cut short are not lived
        h.cat->attached = want && *h.brain_ok;
        send_status(web, h);
    });
    web.on("/api/adopt", HTTP_POST, [&web] {
        send_error(web, 501, "adopting reads the game's save: not on the C3 yet (create a new cat, or adopt on the PC and export it)");
    });
    web.onNotFound([&web, &h] {                           // /api/cats/<id>, /api/brain/<id>
        String u = web.uri();
        bool cats = u.startsWith("/api/cats/"), brain = u.startsWith("/api/brain/");
        if ((cats || brain) && (!has_cat(h) || u.substring(u.lastIndexOf('/') + 1) != h.id))
            send_error(web, 404, "no cat " + u.substring(u.lastIndexOf('/') + 1) + " lives here");
        else if (cats) send_cat_view(web, h);
        else if (brain) send_brain(web, h);
        else web.send(404, "application/json", "{\"error\":\"no route\"}");
    });
}

}  // namespace otomo
