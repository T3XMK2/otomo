/* The placed TRAPS (the lab's objects.py scan + traps, as the lab body sends them): the nodes of the object area
 * whose owner (+0x10) is the cat's or the hunter's record, a type in the game's static code, links inside the area -
 * the traps among them by TYPE, in address order. Pure (checked natively: the lab's body_objs_parity.py). */
#pragma once
#include <stdint.h>
#include "layout.h"

typedef struct { uint32_t addr; uint8_t owner, trap; uint32_t x, z; } Trap;   /* owner: 0 cat, 1 hunter */

/* buf = n bytes of the area read at base; hunter 0 = none; -> traps found (all of them, at most cap kept) */
int scan_traps(const uint8_t *buf, uint32_t base, uint32_t n, uint32_t cat, uint32_t hunter, Trap *out, int cap);
