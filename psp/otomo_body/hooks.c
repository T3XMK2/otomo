/* the lab's hook.py's caves, word for word, for a block at any base (see hooks.h). */
#include "hooks.h"
#include "layout.h"

enum { ZERO = 0, V0 = 2, V1 = 3, A0 = 4, A1 = 5, A3 = 7, T0 = 8, T1 = 9, T2 = 10, T3 = 11, T4 = 12, T5 = 13, T6 = 14,
       T7 = 15, S1 = 17, S3 = 19, S5 = 21, RA = 31 };
#define NOP 0u
#define THINK_END ENTER_BEHAVIOUR          /* think occupies [THINK, ENTER_BEHAVIOUR) */

static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
}
static uint32_t lui(int rt, uint32_t imm) { return I(0x0F, 0, rt, imm); }
static uint32_t lbu(int rt, uint32_t off, int b) { return I(0x24, b, rt, off); }
static uint32_t lhu(int rt, uint32_t off, int b) { return I(0x25, b, rt, off); }
static uint32_t lw(int rt, uint32_t off, int b) { return I(0x23, b, rt, off); }
static uint32_t sb(int rt, uint32_t off, int b) { return I(0x28, b, rt, off); }
static uint32_t sh(int rt, uint32_t off, int b) { return I(0x29, b, rt, off); }
static uint32_t sw(int rt, uint32_t off, int b) { return I(0x2B, b, rt, off); }
static uint32_t addiu(int rt, int rs, uint32_t imm) { return I(0x09, rs, rt, imm); }
static uint32_t ori(int rt, int rs, uint32_t imm) { return I(0x0D, rs, rt, imm); }
static uint32_t andi(int rt, int rs, uint32_t imm) { return I(0x0C, rs, rt, imm); }
static uint32_t sltiu(int rt, int rs, uint32_t imm) { return I(0x0B, rs, rt, imm); }
static uint32_t jmp(uint32_t target) { return (0x02u << 26) | ((target >> 2) & 0x3FFFFFFu); }
static uint32_t R(int rs, int rt, int rd, uint32_t fn) { return ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | ((uint32_t)rd << 11) | fn; }
static uint32_t move(int rd, int rs) { return R(rs, 0, rd, 0x21); }
static uint32_t addu(int rd, int rs, int rt) { return R(rs, rt, rd, 0x21); }
static uint32_t or_(int rd, int rs, int rt) { return R(rs, rt, rd, 0x25); }
static uint32_t sltu(int rd, int rs, int rt) { return R(rs, rt, rd, 0x2B); }
static uint32_t jal(uint32_t target) { return (0x03u << 26) | ((target >> 2) & 0x3FFFFFFu); }
static uint32_t sll(int rd, int rt, uint32_t sa) { return ((uint32_t)rt << 16) | ((uint32_t)rd << 11) | (sa << 6); }
static uint32_t hi(uint32_t addr) { return ((addr + 0x8000u) >> 16) & 0xFFFFu; }

/* a tiny assembler: words, labels, forward / backward beq / bne */
typedef struct {
    uint32_t base, *w; int n;
    int lab[8];
    struct { int at, op, rs, rt, lab; } fix[16]; int nfix;
} Asm;
static void emit(Asm *a, uint32_t w) { a->w[a->n++] = w; }
static void label(Asm *a, int l) { a->lab[l] = a->n; }
static void br(Asm *a, int op, int rs, int rt, int l) {
    a->fix[a->nfix].at = a->n; a->fix[a->nfix].op = op; a->fix[a->nfix].rs = rs; a->fix[a->nfix].rt = rt;
    a->fix[a->nfix].lab = l; a->nfix++;
    emit(a, 0);
}
static int done(Asm *a) {
    for (int k = 0; k < a->nfix; k++) {
        int32_t here = (int32_t)(a->base + 4u * (uint32_t)a->fix[k].at);
        int32_t to = (int32_t)(a->base + 4u * (uint32_t)a->lab[a->fix[k].lab]);
        a->w[a->fix[k].at] = I(a->fix[k].op, a->fix[k].rs, a->fix[k].rt, (uint32_t)((to - (here + 4)) >> 2));
    }
    return a->n;
}
#define BEQ 0x04
#define BNE 0x05
static void load_base(Asm *a, int reg, uint32_t block) { emit(a, lui(reg, hi(block))); emit(a, addiu(reg, reg, block & 0xFFFFu)); }

/* hook.py's _decide: at_calls = 0 the lab's form over the function's first two words (cave2), 1 the plugin's form
 * reached from think's own calls (cave_call: no ra check, a plain jump to the untouched function at the end) */
static int decide(uint32_t block, int at_calls, uint32_t *out) {
    enum { L_THINK, L_RESTORE, L_ORIG, L_LOG, L_LOST };
    Asm a = {0};
    a.base = block + (at_calls ? OFF_CAVE_CALL : OFF_CAVE2); a.w = out;
    load_base(&a, T0, block);
    emit(&a, lbu(T1, OFF_CARD_FLAG, T0));
    br(&a, BEQ, T1, ZERO, L_THINK); emit(&a, NOP);
    emit(&a, lw(T2, OFF_LEASE, T0));
    br(&a, BEQ, T2, ZERO, L_RESTORE); emit(&a, NOP);
    emit(&a, lui(T3, hi(QUEST_TIMER)));
    emit(&a, lw(T3, QUEST_TIMER & 0xFFFFu, T3));
    emit(&a, sltu(T4, T3, T2));
    br(&a, BEQ, T4, ZERO, L_THINK); emit(&a, NOP);
    label(&a, L_RESTORE);
    emit(&a, lw(T1, OFF_CARD_REC, T0));
    for (uint32_t k = 0; k < 4; k++) { emit(&a, lbu(T2, OFF_CARD_WEIGHTS + k, T0)); emit(&a, sb(T2, REC_WEIGHTS + k, T1)); }
    emit(&a, lw(T2, OFF_CARD_SKILLS, T0)); emit(&a, sw(T2, REC_SKILLS, T1));
    emit(&a, lw(T2, OFF_CARD_SKILLS + 4, T0)); emit(&a, sw(T2, REC_SKILLS + 4, T1));
    emit(&a, sb(ZERO, OFF_CARD_FLAG, T0));
    label(&a, L_THINK);
    if (!at_calls) {
        emit(&a, lui(T1, THINK >> 16)); emit(&a, ori(T1, T1, THINK & 0xFFFFu));
        emit(&a, sltu(T2, RA, T1));
        br(&a, BNE, T2, ZERO, L_ORIG); emit(&a, NOP);
        emit(&a, lui(T1, THINK_END >> 16)); emit(&a, ori(T1, T1, THINK_END & 0xFFFFu));
        emit(&a, sltu(T2, RA, T1));
        br(&a, BEQ, T2, ZERO, L_ORIG); emit(&a, NOP);
    }
    emit(&a, addiu(T5, ZERO, 0));
    emit(&a, addiu(T7, ZERO, 1));
    emit(&a, lw(T4, OFF_LEASE, T0));
    br(&a, BEQ, T4, ZERO, L_LOG); emit(&a, NOP);
    emit(&a, lui(T6, hi(QUEST_TIMER)));
    emit(&a, lw(T6, QUEST_TIMER & 0xFFFFu, T6));
    emit(&a, sltu(T2, T6, T4));
    br(&a, BNE, T2, ZERO, L_LOST); emit(&a, NOP);
    emit(&a, addiu(T7, ZERO, 0));
    br(&a, BEQ, ZERO, ZERO, L_LOG); emit(&a, NOP);
    label(&a, L_LOST);
    emit(&a, addiu(T5, ZERO, LOST));
    emit(&a, lhu(T2, OFF_LOG_LOST, T0));
    emit(&a, addiu(T2, T2, 1));
    emit(&a, sh(T2, OFF_LOG_LOST, T0));
    label(&a, L_LOG);
    emit(&a, sb(A1, OFF_LOG_PROPOSAL, T0));
    emit(&a, lhu(T4, OFF_LOG_COUNT, T0));
    emit(&a, addiu(T4, T4, 1));
    emit(&a, sh(T4, OFF_LOG_COUNT, T0));
    emit(&a, lbu(T3, OFF_RING_IDX, T0));
    emit(&a, addu(T2, T0, T3));
    emit(&a, or_(T4, A1, T5));
    emit(&a, sb(T4, OFF_RING, T2));
    emit(&a, addiu(T3, T3, 1));
    emit(&a, andi(T3, T3, RING_SIZE - 1));
    emit(&a, sb(T3, OFF_RING_IDX, T0));
    br(&a, BNE, T7, ZERO, L_ORIG); emit(&a, NOP);
    emit(&a, sltiu(T2, A1, TABLE_SIZE));
    br(&a, BEQ, T2, ZERO, L_ORIG); emit(&a, NOP);
    emit(&a, addu(T3, T0, A1));
    emit(&a, lbu(T3, OFF_TABLE, T3));
    emit(&a, addiu(T2, ZERO, EMPTY));
    br(&a, BEQ, T3, T2, L_ORIG); emit(&a, NOP);
    emit(&a, move(A1, T3));
    emit(&a, sb(T3, OFF_LOG_REPLACED, T0));
    label(&a, L_ORIG);
    if (at_calls) {
        emit(&a, jmp(ENTER_BEHAVIOUR)); emit(&a, NOP);
    } else {
        emit(&a, ENTER_ORIGINAL_0);
        emit(&a, jmp(ENTER_BEHAVIOUR + 8));
        emit(&a, ENTER_ORIGINAL_1);
    }
    return done(&a);
}

int cave2_words(uint32_t block, uint32_t *out) { return decide(block, 0, out); }
int cave_call_words(uint32_t block, uint32_t *out) { return decide(block, 1, out); }
int call_patch_words(uint32_t block, uint32_t *out) { out[0] = jal(block + OFF_CAVE_CALL); return 1; }

int cave3_words(uint32_t block, uint32_t *out) {
    enum { L_BACK };
    Asm a = {0};
    a.base = block + OFF_CAVE3; a.w = out;
    load_base(&a, A0, block);
    emit(&a, lw(A1, OFF_NAME_SRC, A0));
    emit(&a, move(A3, S1));
    br(&a, BNE, A1, S1, L_BACK); emit(&a, NOP);
    emit(&a, lw(A1, OFF_LEASE, A0));
    br(&a, BEQ, A1, ZERO, L_BACK); emit(&a, NOP);
    emit(&a, lui(V0, hi(QUEST_TIMER)));
    emit(&a, lw(V0, QUEST_TIMER & 0xFFFFu, V0));
    emit(&a, sltu(V1, V0, A1));
    br(&a, BNE, V1, ZERO, L_BACK); emit(&a, NOP);
    emit(&a, addiu(A3, A0, OFF_NAME_BUF));
    label(&a, L_BACK);
    emit(&a, jmp(NAME_RETURN));
    emit(&a, NOP);
    return done(&a);
}

int enter_patch_words(uint32_t block, uint32_t *out) { out[0] = jmp(block + OFF_CAVE2); out[1] = NOP; return 2; }
int name_patch_words(uint32_t block, uint32_t *out) { out[0] = jmp(block + OFF_CAVE3); return 1; }

int cave4_words(uint32_t block, uint32_t *out) {
    Asm a = {0};
    a.base = block + OFF_CAVE4; a.w = out;
    load_base(&a, T0, block);
    emit(&a, lbu(T1, OFF_HIT_IDX, T0));
    emit(&a, sll(T2, T1, 2)); emit(&a, sll(T3, T1, 3));
    emit(&a, addu(T2, T2, T3));
    emit(&a, addu(T2, T2, T0));
    emit(&a, lw(T3, 0x10, S5));
    emit(&a, sw(T3, OFF_HIT_RING, T2));
    emit(&a, sw(S3, OFF_HIT_RING + 4, T2));
    emit(&a, sw(S1, OFF_HIT_RING + 8, T2));
    emit(&a, addiu(T1, T1, 1));
    emit(&a, andi(T1, T1, HIT_RING_N - 1));
    emit(&a, sb(T1, OFF_HIT_IDX, T0));
    emit(&a, jmp(HIT_CALLEE));
    emit(&a, NOP);
    return done(&a);
}

int hit_patch_words(uint32_t block, uint32_t *out) { out[0] = jal(block + OFF_CAVE4); return 1; }
