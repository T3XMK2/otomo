/* otomo_body - the cat's BODY inside the game (owner, 4 Oct 2026: ONE way, no PC program; docs/FELYNE_PROTOCOL.md).
 * The same .prx for PPSSPP and a real PSP. It knows only the game's ADDRESSES (layout.h, generated from the Python
 * modules): it reads raw facts, sends them to the cat's home (the ESP32-C3) as SENSE frames over the PSP's own Wi-Fi,
 * and writes the table the mind answers plus its heartbeat lease into the game. The mind (the C3) decides and learns.
 *
 * Stage B3 (5 Oct): the placed TRAPS - objs.c scans the object area like objects.py (owner, type, links); the first 4
 * traps go with each frame (the cat's vs the player's by owner AND type).
 * Stage B2 (5 Oct): the HITS - one word at the hit registration (the jal goes through cave4: who struck, which
 * monster, the raw damage into a ring); each frame carries the first 8 waiting, the rest wait (the lab body's queue).
 * The quest's id (its header) goes with the BYE.
 * The neural cat is a BRAIN on a real game comrade as it is (owner, 5 Oct, option 2): the body never writes the
 * comrade's record nor its name - the game's cat keeps its own name, temperament, skills, level and stats; the body only
 * sends that cat's hall card with each quest's HELLO (who it is, how it grows) and applies the mind's table.
 * Stage B1 (5 Oct): its own memory block for the hook (any base: hooks.c = hook.py's words, checked natively), the
 * enter-behaviour hook installed whenever the game's code holds the original instructions (a quest's code loads and
 * unloads: checked every tick), the senses (cat, hunter, monsters, timer, proposals), SENSE -> TABLE -> table + lease,
 * HELLO at each quest, BYE at its end. B2 the hits and B3 the placed traps are below.
 * Safety as the lab's (hook v3): no TABLE for lease_ms -> the game's cat (the cave checks the lease against the quest
 * timer by itself); the lease is cleared between quests. No libc: system calls only. Its progress is a status block
 * the PC finds in RAM by the magic "OTOMOBD1". */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <psputils.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "hooks.h"
#include "objs.h"
#include "layout.h"
#include "proto.h"

PSP_MODULE_INFO("otomo_body", PSP_MODULE_USER, 1, 0);

#define CFG "ms0:/PSP/PLUGINS/otomo/otomo_body.cfg"     /* loaded by otomo_boot from that folder */
#define SO_NONBLOCK_PSP 0x1009
#define BODY_KIND 1                 /* protocol: 1 = the plugin (PPSSPP or a real PSP: the same file) */
#define BODY_VERSION 1
#define TICK_US (200 * 1000)
#define ANSWER_WAIT_US (150 * 1000)

enum { ST_THREAD = 1, ST_WAITED, ST_NET, ST_SOCKET, ST_BLOCK, ST_RUNNING };

typedef struct {
    char magic[8];                  /* "OTOMOBD1" */
    volatile int step, err, err_step;
    volatile unsigned int block;    /* the hook's block (its base) */
    volatile int installs;          /* times the enter-behaviour hook went in (once per quest) */
    volatile int in_quest;
    volatile int quests;            /* quests seen to their end (a BYE sent) */
    volatile int hellos_sent, hellos_heard;
    volatile int senses_sent, tables_heard, tables_late;
    volatile int proposals;         /* proposals read from the ring */
    volatile int attached;          /* the mind's last word */
    volatile unsigned int lease;    /* the last lease written */
    volatile unsigned int last_ack;
    volatile unsigned int word0, word1;   /* the last words read at ENTER_BEHAVIOUR (what hook_state decided on) */
    volatile int hit_hook;          /* the hit hook is in (this quest) */
    volatile int hits, hits_cat;    /* hits read from the ring (all / the cat's) */
    volatile int quest_id;          /* the quest's id (its header), sent with the BYE */
    volatile int net_ups, net_downs, net_fails;   /* the network: opened per quest, closed after its BYE */
    volatile int traps_now, traps_cat;  /* traps in the last frame (all / the cat's) */
    volatile int sites;             /* think's calls to enter-behaviour found (THINK_CALLS = hooked) */
    char mind[17];                  /* the cat's name from the mind's HELLO */
} Status;

Status g_status = { {'O', 'T', 'O', 'M', 'O', 'B', 'D', '1'} };

static void fail(int step, int err) { g_status.err_step = step; g_status.err = err; }

#define U8(a) (*(volatile uint8_t *)(a))
#define U16(a) (*(volatile uint16_t *)(a))
#define U32(a) (*(volatile uint32_t *)(a))

static int user_ptr(uint32_t p) { return p >= 0x08800000u && p < 0x0A000000u; }

/* ------------------------------------------------------------------------------------------------ the network */
static int g_sock = -1;
static struct sockaddr_in g_mind;

static unsigned short swap16(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }

static int read_cfg(unsigned int *ip, int *port) {            /* "a.b.c.d port" */
    char buf[64];
    SceUID fd = sceIoOpen(CFG, PSP_O_RDONLY, 0);
    if (fd < 0) return fd;
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return -1;
    unsigned int part[5] = {0, 0, 0, 0, 0};
    int k = 0;
    for (int i = 0; i < n && k < 5; i++) {
        char c = buf[i];
        if (c >= '0' && c <= '9') part[k] = part[k] * 10 + (unsigned int)(c - '0');
        else if (c == '.' || c == ' ' || c == ':') k++;
        else if (c == '\r' || c == '\n') break;
    }
    if (k < 4) return -2;
    *ip = part[0] | (part[1] << 8) | (part[2] << 16) | (part[3] << 24);
    *port = (int)part[4];
    return 0;
}

/* what WE opened, so only that is closed again (the game may have opened some itself, e.g. for ad-hoc) */
static struct { int common, inet, net, inet_init, apctl; } g_opened;

static int net_up(void) {
    unsigned int ip = 0;
    int port = 0, r = read_cfg(&ip, &port);
    if (r < 0) { fail(ST_WAITED, r); return r; }
    if (port == 0) port = OT_PORT;
    g_opened.common = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON) == 0;   /* already loaded: not ours */
    g_opened.inet = sceUtilityLoadNetModule(PSP_NET_MODULE_INET) == 0;
    r = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024); g_opened.net = r == 0; if (r < 0) fail(ST_NET, r);
    r = sceNetInetInit(); g_opened.inet_init = r == 0; if (r < 0) fail(ST_NET, r);
    r = sceNetApctlInit(0x8000, 48); g_opened.apctl = r == 0; if (r < 0) fail(ST_NET, r);
    r = sceNetApctlConnect(1); if (r < 0) fail(ST_NET, r);       /* the first saved connection */
    int state = 0;
    for (int i = 0; i < 300; i++) {                               /* bounded: 30 s for an address */
        sceNetApctlGetState(&state);
        if (state == PSP_NET_APCTL_STATE_GOT_IP) break;
        sceKernelDelayThread(100 * 1000);
    }
    if (state != PSP_NET_APCTL_STATE_GOT_IP) fail(ST_NET, -100 - state);   /* an emulator may not report it: go on */
    g_status.step = ST_NET;
    g_sock = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { fail(ST_SOCKET, sceNetInetGetErrno()); return -3; }
    int one = 1;
    sceNetInetSetsockopt(g_sock, SOL_SOCKET, SO_NONBLOCK_PSP, &one, sizeof(one));
    char *z = (char *)&g_mind;
    for (unsigned int i = 0; i < sizeof(g_mind); i++) z[i] = 0;
    g_mind.sin_len = sizeof(g_mind);
    g_mind.sin_family = AF_INET;
    g_mind.sin_port = swap16((unsigned short)port);
    g_mind.sin_addr.s_addr = ip;
    g_status.step = ST_SOCKET;
    g_status.net_ups++;
    return 0;
}

/* after a quest's BYE: the network closed again (PPSSPP refuses savestate saves AND loads while a game uses it; a real
 * PSP's Wi-Fi rests in the village) - only what net_up opened */
static void net_down(void) {
    if (g_sock >= 0) { sceNetInetClose(g_sock); g_sock = -1; }
    if (g_opened.apctl) { sceNetApctlDisconnect(); sceNetApctlTerm(); }
    if (g_opened.inet_init) sceNetInetTerm();
    if (g_opened.net) sceNetTerm();
    if (g_opened.inet) sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    if (g_opened.common) sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    g_opened.common = g_opened.inet = g_opened.net = g_opened.inet_init = g_opened.apctl = 0;
    g_status.net_downs++;
}

static void send_bytes(const uint8_t *p, int n) {
    sceNetInetSendto(g_sock, p, n, 0, (struct sockaddr *)&g_mind, sizeof(g_mind));
}

static int recv_bytes(uint8_t *p, int cap) {                     /* non-blocking: <= 0 = nothing */
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    return sceNetInetRecvfrom(g_sock, p, cap, 0, (struct sockaddr *)&from, &fl);
}

/* ------------------------------------------------------------------------------------------------ the hook's block */
static uint32_t g_block;

static void flush(uint32_t a, uint32_t n) {
    sceKernelDcacheWritebackRange((void *)a, n);
    sceKernelIcacheInvalidateRange((void *)a, n);
}

static int make_block(void) {
    SceUID id = sceKernelAllocPartitionMemory(2, "otomo_body", PSP_SMEM_High, BLOCK_SIZE, NULL);
    if (id < 0) { fail(ST_BLOCK, id); return id; }
    g_block = (uint32_t)sceKernelGetBlockHeadAddr(id);
    for (uint32_t i = 0; i < BLOCK_SIZE; i += 4) U32(g_block + i) = 0;
    for (uint32_t i = 0; i < TABLE_SIZE; i++) U8(g_block + OFF_TABLE + i) = EMPTY;
    U8(g_block + OFF_LOG_PROPOSAL) = EMPTY; U8(g_block + OFF_LOG_REPLACED) = EMPTY;
    uint32_t w[CAVE_MAX];
    int n = cave_call_words(g_block, w);          /* think's calls go through it (cave2 = the lab's form: not here) */
    for (int i = 0; i < n; i++) U32(g_block + OFF_CAVE_CALL + 4 * i) = w[i];
    n = cave4_words(g_block, w);
    for (int i = 0; i < n; i++) U32(g_block + OFF_CAVE4 + 4 * i) = w[i];
    flush(g_block, BLOCK_SIZE);
    g_status.block = g_block;
    g_status.step = ST_BLOCK;
    return 0;
}

/* the game's cat again: no lease, an empty table (between quests, and whenever the mind is gone) */
static void quiet(void) {
    U32(g_block + OFF_LEASE) = 0;
    for (uint32_t i = 0; i < TABLE_SIZE; i++) U8(g_block + OFF_TABLE + i) = EMPTY;
    g_status.lease = 0;
}

/* PPSSPP's JIT writes a marker (an 'emuhack', opcode 0x1A) over the FIRST word of every block it compiled, so code
 * read from inside the game may show that marker instead of the instruction (5 Oct, live). A real PSP has no markers. */
#define EMUHACK(w) (((w) & 0xFC000000u) == 0x68000000u)

/* the quest's code is there: enter-behaviour's own first words (never patched by the body: debt D9) */
static int quest_code(void) {
    uint32_t a = U32(ENTER_BEHAVIOUR), b = U32(ENTER_BEHAVIOUR + 4);
    g_status.word0 = a; g_status.word1 = b;
    return b == ENTER_ORIGINAL_1 && (a == ENTER_ORIGINAL_0 || EMUHACK(a));
}

/* think's THINK_CALLS calls to enter-behaviour (5 Oct, debt D9): each one word, each patched alone (atomic) */
static uint32_t g_sites[THINK_CALLS];
static int g_nsites, g_refused;

/* 0 = no quest code, 1 = our calls are in, 2 = to be hooked (a quest's code came in: original calls) */
static int hook_state(void) {
    if (!quest_code()) { g_nsites = 0; g_refused = 0; return 0; }
    if (g_nsites != (int)THINK_CALLS) return g_refused ? 0 : 2;
    uint32_t p[1];
    call_patch_words(g_block, p);
    int orig = 0;
    for (int i = 0; i < g_nsites; i++) {
        uint32_t w = U32(g_sites[i]);
        if (w == p[0] || EMUHACK(w)) continue;   /* ours (a marker over a patched call: still ours) */
        orig++;
    }
    return orig ? 2 : 1;                         /* an original call back: the quest's code was loaded again */
}

/* the cave the patch would jump to holds exactly its words (checked before EVERY patch: the words are checked equal
 * to Python's natively, this checks they are really in the block - 5 Oct, live: cave4 had never been written, the hit
 * jal slid through zeros out of the block and the emulated CPU stopped) */
static int cave_ok(uint32_t off, int (*words)(uint32_t, uint32_t *)) {
    uint32_t w[CAVE_MAX];
    int n = words(g_block, w);
    for (int i = 0; i < n; i++) {               /* (a JIT marker stands where PPSSPP compiled a block of it) */
        uint32_t got = U32(g_block + off + 4 * i);
        if (got != w[i] && !EMUHACK(got)) { g_status.err = (int)off; return 0; }
    }
    return 1;
}

static void install(void) {
    if (!cave_ok(OFF_CAVE_CALL, cave_call_words)) return;
    uint32_t p[1], orig = (0x03u << 26) | ((ENTER_BEHAVIOUR >> 2) & 0x3FFFFFFu);   /* jal enter-behaviour */
    call_patch_words(g_block, p);
    flush(THINK, ENTER_BEHAVIOUR - THINK);         /* PPSSPP: its compiled blocks go, the true words come back */
    int n = 0;
    for (uint32_t a = THINK; a < ENTER_BEHAVIOUR; a += 4) {
        uint32_t w = U32(a);
        if (w == orig || w == p[0]) { if (n < (int)THINK_CALLS) g_sites[n] = a; n++; }
    }
    g_status.sites = n;
    if (n != (int)THINK_CALLS) { g_nsites = 0; g_refused = 1; return; }   /* not the code we know: no hook */
    quiet();
    for (int i = 0; i < n; i++) if (U32(g_sites[i]) == orig) U32(g_sites[i]) = p[0];   /* one word each */
    flush(THINK, ENTER_BEHAVIOUR - THINK);
    g_nsites = n;
    g_status.installs++;
}

/* ------------------------------------------------------------------------------------------------ the senses */
static const uint8_t *hunter_record(void) {      /* the first valid player slot (hunter/ram/reader.py) */
    for (uint32_t s = 0; s < PLAYER_SLOTS; s++) {
        uint32_t a = PLAYER_TABLE + s * PLAYER_STRIDE;
        uint32_t hp = U16(a + P_HP);
        int st = (int16_t)U16(a + P_STAMINA), stmax = (int16_t)U16(a + P_STAMINA_MAX);
        if (hp <= HP_MAX && stmax >= 75 && stmax <= 450 && st >= 0 && st <= stmax) return (const uint8_t *)a;
    }
    return 0;
}

static Facts g_facts;
static uint8_t g_out[MAX_BYTES], g_in[256];
static uint32_t g_seq;
static uint64_t g_t0;
static uint8_t g_ring_at;

static uint32_t now_ms(void) { return (uint32_t)((sceKernelGetSystemTimeWide() - g_t0) / 1000); }

/* the equipped comrade among the active cards (0x02), -1 if not exactly one: the neural cat's BODY (5 Oct) */
static int equipped_card(void) {
    uint32_t g = U32(GLOBAL), k = 0;
    int n = 0, at = -1;
    if (!user_ptr(g)) return -1;
    for (; k < SLOT_COUNT; k++) if (U8(g + SLOTS_OFF + k * SLOT_STRIDE) & EQUIPPED) { at = (int)k; n++; }
    return n == 1 ? at : -1;
}

static void hello(void) {                       /* with the body's hall card and its training (raw: the mind reads it) */
    int k = equipped_card();
    uint32_t card = k < 0 ? 0 : U32(GLOBAL) + SLOTS_OFF + (uint32_t)k * SLOT_STRIDE;
    uint8_t training = k < 0 ? 0 : U8(U32(GLOBAL) + SLOTS_OFF + TRAINING_OFF + (uint32_t)k);
    send_bytes(g_out, encode_hello_body(g_out, g_seq++, "ULES01213", BODY_KIND, BODY_VERSION,
                                        k < 0 ? 0 : (const uint8_t *)card, training));
    g_status.hellos_sent++;
}

static void heard(int type, const TableMsg *t, const HelloMind *h, uint32_t asked) {
    if (type == (int)T_HELLO) {
        for (int i = 0; i < 17; i++) g_status.mind[i] = h->name[i];
        g_status.hellos_heard++;
    } else if (type == (int)T_TABLE) {
        if (t->ack != asked) { g_status.tables_late++; return; }
        g_status.tables_heard++;
        g_status.attached = t->attached;
        g_status.last_ack = t->ack;
        for (uint32_t i = 0; i < TABLE_SIZE; i++) U8(g_block + OFF_TABLE + i) = t->attached ? t->table[i] : EMPTY;   /* the table first, */
        uint32_t timer = U32(QUEST_TIMER), ticks = (uint32_t)t->lease_ms * 30u / 1000u;    /* then its lease */
        uint32_t lease = timer > ticks + 1 ? timer - ticks : 1;
        U32(g_block + OFF_LEASE) = lease;
        g_status.lease = lease;
    }
}

/* ------------------------------------------------------------------------------------------------ the hits */
static uint8_t g_hit_at;
static struct { uint32_t att, mon; int32_t dmg; } g_hitq[64];
static int g_hitq_n;

static void hit_hook_in(void) {                 /* one word, in the quest's code: (re)installed every quest */
    if (!cave_ok(OFF_CAVE4, cave4_words)) return;
    uint32_t w[1];
    hit_patch_words(g_block, w);
    uint32_t now = U32(HIT_SITE);
    if (now == w[0]) { g_status.hit_hook = 1; return; }
    if (EMUHACK(now)) { flush(HIT_SITE, 4); now = U32(HIT_SITE); }   /* PPSSPP: the marker of a compiled block */
    if (now == w[0]) { g_status.hit_hook = 1; return; }               /* (it was ours under the marker) */
    if (now != HIT_SITE_ORIGINAL) { g_status.hit_hook = 0; return; }  /* not this quest's code (yet) */
    U32(HIT_SITE) = w[0];                       /* one aligned word: atomic */
    flush(HIT_SITE, 4);
    g_status.hit_hook = 1;
}

static void hits_read(void) {                   /* the ring since the last tick -> the queue (oldest dropped if full) */
    uint8_t idx = U8(g_block + OFF_HIT_IDX);
    while (g_hit_at != idx) {
        uint32_t e = g_block + OFF_HIT_RING + HIT_ENTRY * g_hit_at;
        if (g_hitq_n == 64) { for (int i = 1; i < 64; i++) g_hitq[i - 1] = g_hitq[i]; g_hitq_n--; }
        g_hitq[g_hitq_n].att = U32(e); g_hitq[g_hitq_n].mon = U32(e + 4); g_hitq[g_hitq_n].dmg = (int32_t)U32(e + 8);
        g_hitq_n++;
        g_status.hits++;
        g_hit_at = (uint8_t)((g_hit_at + 1) & (HIT_RING_N - 1));
    }
}

static void sense_and_answer(void) {
    Facts *f = &g_facts;
    uint32_t cat = U32(CAT_PTR);
    f->body_ms = now_ms();
    f->timer = U32(QUEST_TIMER);
    f->lease_ok = 1;
    f->lost_total = U16(g_block + OFF_LOG_LOST);
    f->cat = (const uint8_t *)cat;
    f->hunter = hunter_record();
    f->n_mon = 0;
    uint32_t list = U32(MONSTER_LIST_PTR);
    if (user_ptr(list))
        for (uint32_t i = 0; i < MONSTER_LIST_N && f->n_mon < (int)MAX_MONSTERS; i++) {
            uint32_t p = U32(list + MONSTER_LIST_OFF + 4 * i);
            if (user_ptr(p)) { f->mon_addr[f->n_mon] = p; f->mon[f->n_mon] = (const uint8_t *)p; f->n_mon++; }
        }
    f->n_prop = 0;                                  /* the ring: everything since the last tick (a lap = 64) */
    uint8_t idx = U8(g_block + OFF_RING_IDX);
    while (g_ring_at != idx && f->n_prop < 64) {
        f->prop[f->n_prop++] = U8(g_block + OFF_RING + g_ring_at);
        g_ring_at = (uint8_t)((g_ring_at + 1) & (RING_SIZE - 1));
    }
    g_status.proposals += f->n_prop;
    hits_read();                                    /* B2: the first 8 waiting go, the rest wait (hitlog's queue) */
    int take = g_hitq_n < (int)MAX_HITS ? g_hitq_n : (int)MAX_HITS;
    f->n_hit = take;
    for (int i = 0; i < take; i++) {
        uint32_t a = g_hitq[i].att;
        f->hit[i].who = a == cat ? 0 : (f->hunter && a == (uint32_t)f->hunter) ? 1 : 2;   /* hitlog.who */
        if (f->hit[i].who == 0) g_status.hits_cat++;
        f->hit[i].idx = 255;
        for (int k = 0; k < f->n_mon; k++) if (f->mon_addr[k] == g_hitq[i].mon) { f->hit[i].idx = (uint8_t)k; break; }
        int32_t d = g_hitq[i].dmg;
        f->hit[i].dmg = (uint16_t)(d < 0 ? 0 : d > 0xFFFF ? 0xFFFF : d);
    }
    for (int i = take; i < g_hitq_n; i++) g_hitq[i - take] = g_hitq[i];
    g_hitq_n -= take;
    {                                               /* B3: the placed traps (objects.py's scan) */
        Trap tr[MAX_OBJECTS];
        int n = scan_traps((const uint8_t *)POOL_LO, POOL_LO, POOL_HI - POOL_LO, cat,
                           f->hunter ? (uint32_t)f->hunter : 0, tr, MAX_OBJECTS);
        f->n_obj = n < (int)MAX_OBJECTS ? n : (int)MAX_OBJECTS;
        g_status.traps_now = n; g_status.traps_cat = 0;
        for (int i = 0; i < f->n_obj; i++) {
            f->obj[i].addr = tr[i].addr; f->obj[i].owner = tr[i].owner; f->obj[i].trap = tr[i].trap;
            f->obj[i].x = tr[i].x; f->obj[i].z = tr[i].z;
            g_status.traps_cat += tr[i].owner == 0;
        }
    }
    uint32_t seq = g_seq++;
    send_bytes(g_out, encode_sense(g_out, seq, f));
    g_status.senses_sent++;
    for (int waited = 0; waited < ANSWER_WAIT_US; waited += 5000) {   /* bounded: the answer, or the lease runs */
        int n = recv_bytes(g_in, sizeof(g_in));
        if (n > 0) {
            uint32_t s; TableMsg t; HelloMind h;
            int type = decode_mind(g_in, n, &s, &t, &h);
            heard(type, &t, &h, seq);
            if (type == (int)T_TABLE && t.ack == seq) return;
            continue;
        }
        sceKernelDelayThread(5000);
    }
}

static void bye(int quest_no) {
    char quest[16] = "quest ";                      /* "quest <id>" from the quest's header */
    {
        int k = 6, n = g_status.quest_id;
        char d[8]; int m = 0;
        if (n == 0) d[m++] = '0';
        while (n > 0 && m < 7) { d[m++] = (char)('0' + n % 10); n /= 10; }
        while (m > 0) quest[k++] = d[--m];
        quest[k] = 0;
    }
    char run[32] = "plugin-";
    int k = 7, n = quest_no;
    char d[12]; int m = 0;
    if (n == 0) d[m++] = '0';
    while (n > 0) { d[m++] = (char)('0' + n % 10); n /= 10; }
    while (m > 0 && k < 31) run[k++] = d[--m];
    run[k] = 0;
    for (int tries = 0; tries < 3; tries++) {       /* bounded: the mind answers BYE, else it is told 3 times */
        send_bytes(g_out, encode_bye_body(g_out, g_seq++, quest, run, 0));
        for (int waited = 0; waited < 500 * 1000; waited += 10000) {
            int r = recv_bytes(g_in, sizeof(g_in));
            uint32_t s;
            if (r > 0 && decode_mind(g_in, r, &s, 0, 0) == (int)T_BYE) return;
            sceKernelDelayThread(10000);
        }
    }
}

static int g_by_loader;                             /* started by otomo_boot at a quest: no boot wait */

static int body_thread(SceSize args, void *argp) {
    g_status.step = ST_THREAD;
    if (!g_by_loader) sceKernelDelayThread(15 * 1000 * 1000);   /* booted as a plugin itself: let the game boot first */
    g_status.step = ST_WAITED;
    if (make_block() < 0) return 0;
    g_t0 = sceKernelGetSystemTimeWide();
    int was_in = 0, net = 0;
    for (;;) {
        uint32_t cat = U32(CAT_PTR), timer = U32(QUEST_TIMER);
        int hs = hook_state();
        if (hs == 2) {                              /* a quest's code came in: think's original calls */
            install();
            g_ring_at = U8(g_block + OFF_RING_IDX);
            hs = hook_state();
        }
        int in = hs == 1 && user_ptr(cat) && timer != 0;
        if (in && !was_in) {                        /* a new quest: the network, then the mind sees its owner afresh */
            if (!net) {
                if (net_up() == 0) { net = 1; g_status.step = ST_RUNNING; }
                else { net_down(); g_status.net_fails++; }   /* this quest: the game's cat (no lease) */
            }
            g_status.quest_id = U16(QUEST_ID);
            g_status.hit_hook = 0;
            g_hit_at = U8(g_block + OFF_HIT_IDX);   /* this quest's hits only */
            g_hitq_n = 0;
            hello();
        }
        if (!in && was_in) {                        /* the quest is over: the record is a quest copy, not written */
            quiet();
            g_status.quests++;
            if (net) { bye(g_status.quests - 1); net_down(); net = 0; }
        }
        g_status.in_quest = in;
        was_in = in;
        if (in && net) {
            hit_hook_in();                          /* its code may come in after the quest's start */
            sense_and_answer();
        } else if (net) {
            int n = recv_bytes(g_in, sizeof(g_in));  /* (late answers, a HELLO) */
            if (n > 0) { uint32_t s; TableMsg t; HelloMind h; int type = decode_mind(g_in, n, &s, &t, &h); if (type == (int)T_HELLO) heard(type, &t, &h, 0); }
        }
        sceKernelDelayThread(TICK_US);
    }
    return 0;
}

int module_start(SceSize args, void *argp) {
    g_by_loader = args == sizeof(int) && argp && *(int *)argp == 1;
    SceUID th = sceKernelCreateThread("otomo_body", body_thread, 0x30, 0x4000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
