// otomo_link - the cat's home (ESP32-C3 SuperMini). The ONE way (owner, 4 Oct 2026): the body inside the game talks to
// this board over the home Wi-Fi with protocol v1 (docs/FELYNE_PROTOCOL.md); THE CAT lives here (otomo_cat.h): it
// decides and LEARNS exactly like the Python cat (the lab's mind_parity.py, the lab's mind_learn_parity.py), its brain in
// flash (LittleFS /brain.bin, v2, + /diary.bin, /passport.json), saved every minute it learnt something. The web
// server (port 80) is its home page (otomo_web.h, the studio's routes) + /status, /brain.bin, /diary.bin for the lab.
// ONE cat lives here: created here (a newborn) or exported from the PC; released = archived in /released/, never
// deleted, and it can come back. Attached by the owner's button on the page (or the lab's VERIFY mode).
// Wi-Fi: the network saved by an earlier join, else include/wifi_secrets.h (typed by the owner), else WPS (bounded).
#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <esp_wps.h>

#include "otomo_cat.h"
#include "otomo_diary.h"
#include "otomo_web.h"
#include "protocol.h"
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

static const uint16_t PORT = 7777;
static const uint16_t VERIFY = 0x8000;                    // a lab HELLO with this version bit: deterministic draws
static WiFiUDP udp;
static WebServer web(80);
static volatile bool wps_done = false, wps_failed = false;

static otomo::TheCat cat;                                    // ~90 KB: the brain that learns, the owner, a minute of life
static bool brain_ok = false, verify = false;
static uint32_t heard = 0, frames = 0, bad = 0, out_seq = 0, us_sum = 0, us_max = 0, lessons = 0, saved_step = 0;
static uint32_t last_save = 0, live_us_max = 0, last_frame_ms = 0;
static uint32_t lived_at_start = 0, bye_lived = 0;        // the quest in progress (HELLO .. BYE)
static double bye_attached_s = 0;
static bool quest_open = false;
static otomo::Diary diary;                                  // what it remembers for its owner (/diary.bin)
static otomo::Home home;                                    // the page's view of the cat living here
static uint8_t card_weights[4] = {50, 50, 50, 50};       // its card (passport): what the body swaps in
static uint64_t card_skills = 0;
static uint8_t brain_buf[40000];                          // brain.bin v2 is 36 KB

static void on_wifi(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WPS_ER_SUCCESS) {
        esp_wifi_wps_disable();
        wps_done = true;
        WiFi.begin();                                     // the credentials WPS gave, saved by the Wi-Fi driver
    } else if (event == ARDUINO_EVENT_WPS_ER_FAILED || event == ARDUINO_EVENT_WPS_ER_TIMEOUT) {
        esp_wifi_wps_disable();
        wps_failed = true;
    }
}

static bool wait_connected(uint32_t ms) {                 // bounded
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) delay(200);
    return WiFi.status() == WL_CONNECTED;
}

static void join() {
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(on_wifi);
    WiFi.begin();                                         // the network saved by an earlier join (if any)
    if (wait_connected(8000)) return;
#ifdef WIFI_SSID
    Serial.println("joining " WIFI_SSID " (wifi_secrets.h)");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (wait_connected(15000)) return;
#endif
    for (int attempt = 0; attempt < 3 && WiFi.status() != WL_CONNECTED; attempt++) {   // bounded: 3 x 2 min
        Serial.println("WPS: press the WPS button on the router now (2 minutes)");
        esp_wps_config_t cfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
        wps_done = wps_failed = false;
        esp_wifi_wps_enable(&cfg);
        esp_wifi_wps_start(0);
        uint32_t t0 = millis();
        while (!wps_done && !wps_failed && millis() - t0 < 125000) delay(200);
        if (wps_done) wait_connected(15000);
    }
}

// the nature its habits are read with (the passport's temperament weights and skills: the body's own, from the game)
static void take_card() {
    JsonArray w = home.passport["card_weights"];
    for (int k = 0; k < 4; k++) card_weights[k] = k < (int)w.size() ? (uint8_t)(w[k] | 50) : 50;
    card_skills = home.passport["skills_mask"] | 0ULL;
    memcpy(home.nature.weights, card_weights, 4); home.nature.skills = card_skills;
}

// the cat living here: its brain, its memories, its passport (none: the game's own cat decides, the page offers New cat)
static void load_cat() {
    brain_ok = false;
    File f = LittleFS.open("/brain.bin", "r");
    size_t n = 0;
    if (f) { n = f.read(brain_buf, sizeof brain_buf); f.close(); brain_ok = otomo::load_v2(brain_buf, n, cat.lb); }
    cat.begin();
    saved_step = cat.lb.step;
    diary.clear();
    File d = LittleFS.open("/diary.bin", "r");             // what it remembers (absent the first time)
    if (d && d.size() == sizeof diary) d.read((uint8_t *)&diary, sizeof diary);
    if (d) d.close();
    diary.dirty = false;
    home.id = ""; home.name = ""; home.nature = otomo::Cat(); home.passport.clear();
    if (otomo::load_passport(home)) take_card();
    Serial.printf("otomo_link: %s, brain %s (%u bytes, %lu lessons)\n", otomo::has_cat(home) ? home.name.c_str() : "no cat",
                  brain_ok ? "loaded" : "none", (unsigned)n, (unsigned long)cat.lb.step);
}

static void save_brain() {                                // atomic: write a new file, then rename it over the old
    size_t n = otomo::save_v2(cat.lb, brain_buf);
    File f = LittleFS.open("/brain.new", "w");
    if (!f) return;
    bool ok = f.write(brain_buf, n) == n;
    f.close();
    if (ok && LittleFS.rename("/brain.new", "/brain.bin")) saved_step = cat.lb.step;
    if (diary.dirty) {                                    // the diary with it (a raw image: same firmware, same layout)
        File d = LittleFS.open("/diary.new", "w");
        if (d && d.write((const uint8_t *)&diary, sizeof diary) == sizeof diary) {
            d.close();
            if (LittleFS.rename("/diary.new", "/diary.bin")) diary.dirty = false;
        } else if (d) d.close();
    }
}

// ---------------------------------------- who lives here (studio.py POST /api/cats, /api/release, /api/return)
static double uniform01() { return (esp_random() + 0.5) / 4294967296.0; }

static String home_full() {
    return String("One cat per home: ") + home.name + " lives here. Release it first (its passport and brain are archived).";
}

static bool id_ok(const char *id) {                       // passport.Passport.check: [a-z0-9][a-z0-9_-]{0,31}
    size_t n = strlen(id);
    if (n < 1 || n > 32 || !(islower((unsigned char)id[0]) || isdigit((unsigned char)id[0]))) return false;
    for (size_t i = 1; i < n; i++)
        if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-' || id[i] == '_')) return false;
    return true;
}

static void reply_cat(const char *key) {                  // {created|returned: id, describe}
    char text[200];
    otomo::describe(home, text, sizeof text);
    JsonDocument r;
    r[key] = home.id; r["describe"] = text;
    String out; serializeJson(r, out);
    web.send(200, "application/json", out);
}

// A NEW BRAIN (owner, 5 Oct, option 2: the neural cat is a brain on a real game comrade as it is): POST /api/cats {}
// arms an empty home - the comrade equipped at the next quest receives the brain (adopt_body); {cancel: true} undoes it.
// The flag is a file (it survives a restart).
static bool waiting() { return LittleFS.exists("/waiting"); }

static void new_brain() {
    if (otomo::has_cat(home)) return otomo::send_error(web, 409, home_full());
    JsonDocument b;
    deserializeJson(b, web.arg("plain"));
    JsonDocument r;
    if (b["cancel"] | false) {
        LittleFS.remove("/waiting");
        r["waiting"] = false;
    } else {
        if (LittleFS.totalBytes() - LittleFS.usedBytes() < 64 * 1024)
            return otomo::send_error(web, 507, "the home's memory is full (released cats are kept in it): no room for a new brain");
        File f = LittleFS.open("/waiting", "w");
        if (!f) return otomo::send_error(web, 500, "could not write /waiting");
        f.print("1");
        f.close();
        r["waiting"] = true;
        r["describe"] = "a new brain: the comrade you take on your next quest receives it";
    }
    String out; serializeJson(r, out);
    web.send(200, "application/json", out);
}

// at a quest's HELLO, the home waiting: the brain is born in the equipped comrade - its passport is the GAME's (name,
// temperament, coat, skills, first leader), its tastes come from its temperament (otomo_cat.h newborn)
static void adopt_body(const uint8_t *card, uint8_t training) {
    const otomo::Meta &m = home.meta;
    char name[32];
    int p = card[0x4B];
    if (!otomo::game_name_at(card[1], name, sizeof name) || p >= m.n_pers) {
        Serial.println("otomo_link: the equipped comrade's card does not read as one of the game's: not adopted");
        return;
    }
    char id[33];
    int k = 0;
    for (const char *c = name; *c && k < 32; c++) if (isalnum((unsigned char)*c)) id[k++] = (char)tolower((unsigned char)*c);
    id[k] = 0;
    if (!k) strlcpy(id, "cat", sizeof id);
    char leader[11];                                      // its first leader (UTF-16 at +0x4E): the hunter who hired it
    for (k = 0; k < 10; k++) { uint8_t c = card[0x4E + 2 * k]; if (!c || card[0x4F + 2 * k]) break; leader[k] = (char)c; }
    leader[k] = 0;
    uint8_t w[4];
    for (int i = 0; i < 4; i++) { int x = m.pers[p].weights[i]; w[i] = x < 0 ? 255 : (uint8_t)x; }
    uint64_t mask = 0;
    for (int i = 0; i < 8; i++) mask |= (uint64_t)card[otomo::S_SKILLS + i] << (8 * i);
    JsonDocument &pp = home.passport;
    pp.clear();
    pp["id"] = id; pp["name"] = name; pp["personality"] = p;
    JsonArray sk = pp["skills"].to<JsonArray>();
    for (int b = 0; b < 64; b++) if ((mask >> b) & 1) sk.add(b);
    pp["owner"] = leader[0] ? leader : "Owner"; pp["origin"] = "game"; pp["adopted_from"] = nullptr;
    char when[20];
    otomo::now_text(when, sizeof when);
    pp["created"] = when;
    JsonObject g = pp["growth"].to<JsonObject>();
    g["quests"] = 0; g["attached_s"] = 0.0; g["decisions_lived"] = 0;
    pp["history"].to<JsonArray>();
    pp["coat"] = card[2];
    JsonArray cw = pp["card_weights"].to<JsonArray>();
    for (int i = 0; i < 4; i++) cw.add(w[i]);
    pp["skills_mask"] = mask;
    bool changed;
    otomo::meet_body(home, card, training, changed);        // 'adopted': its body and the first page of its growth
    cat.attached = false;
    otomo::newborn(cat.lb, w, uniform01);
    cat.begin();
    cat.life.felt = false; cat.life.n_changed = 0;
    diary.clear();
    brain_ok = true;
    save_brain();
    if (saved_step != cat.lb.step || !otomo::save_passport(home)) {
        load_cat();
        Serial.println("otomo_link: the new brain could not be saved in the flash");
        return;
    }
    LittleFS.remove("/waiting");
    home.id = id; home.name = name;
    home.wrong_body = false;
    take_card();
    Serial.printf("otomo_link: %s received a brain\n", name);
}

// POST /api/release {id, confirm: its name}: archived in /released/<id>-<time>/ (passport, brain, memories), never deleted
static void release_cat() {
    JsonDocument b;
    deserializeJson(b, web.arg("plain"));
    String id = b["id"] | "", confirm = b["confirm"] | "";
    if (!otomo::has_cat(home) || id != home.id) return otomo::send_error(web, 404, "no cat " + id + " lives here");
    String want = home.name;
    confirm.trim(); confirm.toLowerCase(); want.toLowerCase();
    if (confirm != want) return otomo::send_error(web, 400, "type the name '" + home.name + "' to release it");
    cat.attached = false;
    cat.life.drop_pending();                              // decisions cut short are not lived
    if (brain_ok) save_brain();                           // it leaves as it is now
    char stamp[20];
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    String dir = "/released/" + home.id + "-" + stamp;
    if (!LittleFS.exists("/released")) LittleFS.mkdir("/released");
    if (LittleFS.exists(dir)) return otomo::send_error(web, 409, "a cat was released here this very second: try again");
    if (!LittleFS.mkdir(dir)) return otomo::send_error(web, 500, "could not make " + dir);
    bool ok = true;
    for (const char *f : {"/passport.json", "/brain.bin", "/diary.bin"})
        if (LittleFS.exists(f)) ok = LittleFS.rename(f, dir + f) && ok;
    load_cat();                                           // what is left at home: nothing, if all went
    cat.attached = false;
    if (!ok || otomo::has_cat(home)) return otomo::send_error(web, 500, "only partly archived in " + dir);
    Serial.printf("otomo_link: %s released -> %s\n", id.c_str(), dir.c_str());
    JsonDocument r;
    r["released"] = id; r["archived"] = dir;
    String out; serializeJson(r, out);
    web.send(200, "application/json", out);
}

// POST /api/return {archive}: a released cat comes home again, as it left - only into an empty home
static void return_cat() {
    if (otomo::has_cat(home)) return otomo::send_error(web, 409, home_full());
    JsonDocument b;
    deserializeJson(b, web.arg("plain"));
    String a = b["archive"] | "", dir = "/released/" + a;
    if (!otomo::archive_name(a.c_str()) || !LittleFS.exists(dir + "/passport.json"))
        return otomo::send_error(web, 400, "no released cat '" + a + "'");
    bool ok = true;
    for (const char *f : {"/passport.json", "/brain.bin", "/diary.bin"})
        if (LittleFS.exists(dir + f)) ok = LittleFS.rename(dir + f, f) && ok;
    if (ok) LittleFS.rmdir(dir);
    load_cat();
    cat.attached = false;
    if (!ok || !otomo::has_cat(home)) return otomo::send_error(web, 500, "it could not come back whole from " + dir);
    Serial.printf("otomo_link: %s is home again\n", home.name.c_str());
    reply_cat("returned");
}

static void web_routes() {
    web.on("/status", [] {                                // the lab's view (the lab's mind_*parity.py)
        char j[384];
        snprintf(j, sizeof j, "{\"brain\":%s,\"lessons\":%lu,\"lived\":%lu,\"frames\":%lu,\"attached\":%s,\"verify\":%s,"
                 "\"table_us_mean\":%lu,\"table_us_max\":%lu,\"live_us_max\":%lu,\"saved_step\":%lu,\"heap\":%lu,\"heap_min\":%lu,\"dropped\":%lu,\"owner_overflow\":%lu}",
                 brain_ok ? "true" : "false", (unsigned long)cat.lb.step, (unsigned long)lessons, (unsigned long)frames,
                 cat.attached ? "true" : "false", verify ? "true" : "false",
                 (unsigned long)(frames ? us_sum / frames : 0), (unsigned long)us_max, (unsigned long)live_us_max,
                 (unsigned long)saved_step, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                 (unsigned long)cat.life.dropped,
                 (unsigned long)(cat.mind.profile.overflow() + cat.mind.style.buf.overflow));   // this quest's
        web.send(200, "application/json", j);
    });
    web.on("/brain.bin", [] {                             // the brain as it is NOW (RAM), brain.bin v2
        size_t n = otomo::save_v2(cat.lb, brain_buf);
        web.send_P(200, "application/octet-stream", (const char *)brain_buf, n);
    });
    web.on("/diary.bin", [] {                             // what it remembers, as it is NOW (a raw image: same firmware)
        web.send_P(200, "application/octet-stream", (const char *)&diary, sizeof diary);
    });
    web.serveStatic("/released/", LittleFS, "/released/");   // the archives, read only (the export keeps them)
    web.on("/api/cats", HTTP_POST, new_brain);
    web.on("/api/release", HTTP_POST, release_cat);
    web.on("/api/return", HTTP_POST, return_cat);
    otomo::web_routes(web, home);                            // the studio page and its views
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("otomo_link: starting");
    home.cat = &cat; home.diary = &diary; home.brain_ok = &brain_ok; home.frames = &frames; home.last_frame_ms = &last_frame_ms;
    if (LittleFS.begin(false)) {
        if (!otomo::load_meta(home)) Serial.println("otomo_link: no /meta.json (pio run -t uploadfs)");
        load_cat();
    } else {
        Serial.println("otomo_link: no file system (pio run -t uploadfs)");
    }
    join();
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);                             // modem sleep added ~50 ms to every answer (bench, 4 Oct)
        udp.begin(PORT);
        web_routes();
        web.begin();
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");   // the clock, for the history's "when"
        Serial.printf("otomo_link: on the network, plugin address = %s %u\n", WiFi.localIP().toString().c_str(), PORT);
    } else {
        Serial.println("otomo_link: NOT connected (no saved network, no wifi_secrets.h, WPS failed)");
    }
}

static uint32_t xs;                                       // VERIFY draws: xorshift32, seeded per frame by its seq
static float draw() {
    if (!verify) return (esp_random() >> 8) / 16777216.0f;
    xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5;
    return (xs >> 8) / 16777216.0f;
}

static void on_lesson(const otomo::Lesson &l) {               // each decision lived: counted, and remembered
    lessons++;
    if (l.entered && l.imitated && l.x)
        diary.remember(cat.lb.net, l.x, l.proposal, l.answer, l.feeling, cat.last_cat, l.step);
}

static void reply(const uint8_t *out, int n) {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(out, n);
    udp.endPacket();
}

static void on_sense(const otomo::Sense &s) {
    uint8_t table[otomo::TABLE_SIZE], out[128];
    uint32_t t0 = micros();
    if (brain_ok) {
        xs = (s.seq * 2654435761u) ^ 0x9E3779B9u;
        if (!xs) xs = 1;
        cat.decide(s, draw, table);
    } else {
        memset(table, otomo::EMPTY, sizeof table);
    }
    last_frame_ms = millis();
    bool on = brain_ok && cat.attached && !home.wrong_body;   // attached (on its own body): its card goes with the table
    reply(out, otomo::encode_table(out, out_seq++, s.seq, 2000, on, table, nullptr, 0));   // no card (option 2)
    uint32_t us = micros() - t0;                          // the answer time the body sees
    us_sum += us;
    if (us > us_max) us_max = us;
    if (brain_ok) {                                       // then the slow part: the owner, learning
        uint32_t t1 = micros();
        cat.live(s, on_lesson);
        if (micros() - t1 > live_us_max) live_us_max = micros() - t1;
    }
}

static void on_datagram(const uint8_t *buf, int len) {
    static otomo::Sense s;                                   // static: off the small loop stack
    int type = otomo::decode_sense(buf, len, s);
    if (type == otomo::T_HELLO) {
        uint16_t ver = len >= 8 + 13 ? (uint16_t)(buf[8 + 11] | (buf[8 + 12] << 8)) : 0;   // body hello: id[10], kind, ver
        verify = (ver & VERIFY) != 0;
        cat.reset_owner();                                // a new body / a new quest: the owner seen afresh
        if (verify) cat.attached = true;                  // the lab's replays; otherwise the owner's button decides
        cat.attached_s = 0; cat.last_t = -1;              // a new quest: its own attached time and lessons
        lived_at_start = cat.life.lived_n; quest_open = true;
        if (len == 8 + 13 + 0x71 && !otomo::has_cat(home) && waiting() && !verify)
            adopt_body(buf + 8 + 13, buf[8 + 13 + 0x70]);  // a new brain meets its body
        else if (len == 8 + 13 + 0x71 && otomo::has_cat(home) && !verify) {   // its body's hall card (5 Oct)
            bool changed = false;
            const char *met = otomo::meet_body(home, buf + 8 + 13, buf[8 + 13 + 0x70], changed);
            home.wrong_body = strcmp(met, "other") == 0;
            if (home.wrong_body && cat.attached) cat.detach();   // never on another game cat
            if (changed) otomo::save_passport(home);
            Serial.printf("H body %s (name %u, level %u)\n", met, buf[8 + 13 + 1], buf[8 + 13 + 0x16] + 1);
        }
        uint8_t out[64];
        bool has = home.name.length() > 0;               // the cat living here: its name (no card: option 2)
        reply(out, otomo::encode_hello_mind(out, out_seq++, 1, has ? home.name.c_str() : "Otomo", nullptr, 0));
        Serial.printf("H seq=%lu verify=%d brain=%d\n", (unsigned long)s.seq, verify, brain_ok);
    } else if (type == otomo::T_SENSE) {
        frames++;
        on_sense(s);
        if (frames % 250 == 0)
            Serial.printf("S %lu frames, %lu lessons lived (step %lu), %lu us mean, %lu us max\n", (unsigned long)frames,
                          (unsigned long)lessons, (unsigned long)cat.lb.step, (unsigned long)(us_sum / frames),
                          (unsigned long)us_max);
    } else if (type == otomo::T_BYE) {                       // the quest is over (5 Oct)
        char quest[17], run[33];
        uint8_t why = 0;
        if (!otomo::decode_bye_body(buf, len, quest, run, why)) { bad++; return; }
        if (quest_open && brain_ok) {                     // once per quest (a repeated BYE only gets the answer again)
            quest_open = false;
            cat.life.close(cat.last_cat, on_lesson);   // the pending ones, lived now
            bye_lived = cat.life.lived_n - lived_at_start;
            bye_attached_s = cat.attached_s;
            if (bye_attached_s > 0 && !verify) {          // it grows only from a quest played attached
                cat.drift_tastes([] { return (esp_random() + 0.5) / 4294967296.0; });
                otomo::record_quest(home, quest, run, bye_attached_s, bye_lived);
                otomo::save_passport(home);
            }
            if (!verify) save_brain();
            Serial.printf("BYE %s: %lu lessons, %.0f s attached, step %lu\n", quest, (unsigned long)bye_lived,
                          bye_attached_s, (unsigned long)cat.lb.step);
        }
        uint8_t out[32];
        reply(out, otomo::encode_bye_mind(out, out_seq++, bye_lived, (float)bye_attached_s, cat.lb.step));
    } else {
        bad++;
    }
}

void loop() {
    static uint32_t last = 0;
    if (WiFi.status() == WL_CONNECTED) {
        for (int k = 0; k < 4; k++) {                     // a few datagrams per turn, then the web server
            int n = udp.parsePacket();
            if (n <= 0) break;
            static uint8_t buf[1024];
            int len = udp.read(buf, sizeof(buf));
            if (len > 0) on_datagram(buf, len);
        }
        web.handleClient();
    }
    // a crash loses at most a minute of experience (never in the lab's VERIFY replays)
    if (brain_ok && !verify && cat.lb.step != saved_step && millis() - last_save > 60000) {
        last_save = millis();
        save_brain();
    }
    if (millis() - last > 5000) {                         // the address, again, for whoever opens the console late
        last = millis();
        Serial.printf("otomo_link: %s %s, brain %d, frames %lu, lessons %lu, refused %lu\n",
                      WiFi.status() == WL_CONNECTED ? "online" : "offline", WiFi.localIP().toString().c_str(),
                      brain_ok, (unsigned long)frames, (unsigned long)lessons, (unsigned long)bad);
    }
    delay(1);
}
