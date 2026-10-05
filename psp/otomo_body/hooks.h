/* The hooks' code for the plugin's own block (any base): the SAME words as the lab's hook.py's cave2_code /
 * cave3_code / enter_patch / name_patch for that base (checked natively: the lab's body_hook_parity.py). Pure: no game
 * access here - main.c writes them and flushes the caches. */
#pragma once
#include <stdint.h>

#define CAVE_MAX 128

/* each returns the number of words written to out (<= CAVE_MAX) */
int cave2_words(uint32_t block, uint32_t *out);   /* the enter-behaviour hook: proposals -> ring, the table, the card */
int cave3_words(uint32_t block, uint32_t *out);   /* the HUD name hook */
int enter_patch_words(uint32_t block, uint32_t *out);   /* 2 words over ENTER_BEHAVIOUR */
int name_patch_words(uint32_t block, uint32_t *out);    /* 1 word over NAME_SITE */
int cave4_words(uint32_t block, uint32_t *out);   /* the hit hook: who struck which monster, the damage */
int hit_patch_words(uint32_t block, uint32_t *out);     /* 1 word over HIT_SITE (the jal goes through the cave) */
int cave_call_words(uint32_t block, uint32_t *out);  /* the decision reached from think's calls (debt D9) */
int call_patch_words(uint32_t block, uint32_t *out);    /* 1 word over each of think's THINK_CALLS calls */
