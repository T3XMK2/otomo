/* See objs.h. */
#include "objs.h"

static uint32_t rd(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int scan_traps(const uint8_t *buf, uint32_t base, uint32_t n, uint32_t cat, uint32_t hunter, Trap *out, int cap) {
    static const uint32_t TYPES[3] = {TRAP_TYPE_0, TRAP_TYPE_1, TRAP_TYPE_2};
    int found = 0;
    for (uint32_t o = 0; o + 0x50 < n; o += 16) {
        uint32_t owner = rd(buf + o + 0x10);
        int who = owner == cat ? 0 : (hunter && owner == hunter) ? 1 : -1;
        if (who < 0) continue;
        uint32_t typ = rd(buf + o), prev = rd(buf + o + 4), next = rd(buf + o + 8);
        if (typ < STATIC_LO || typ >= STATIC_HI) continue;
        if ((prev && (prev < POOL_LO || prev >= POOL_HI)) || (next && (next < POOL_LO || next >= POOL_HI))) continue;
        int trap = -1;
        for (int k = 0; k < 3; k++) if (typ == TYPES[k]) trap = k;
        if (trap < 0) continue;
        if (found < cap) {
            out[found].addr = base + o; out[found].owner = (uint8_t)who; out[found].trap = (uint8_t)trap;
            out[found].x = rd(buf + o + 0x40); out[found].z = rd(buf + o + 0x48);
        }
        found++;
    }
    return found;
}
