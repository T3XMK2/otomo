/* otomo_boot - the cat's body LOADER (5 Oct 2026). The plugin the game boots with: it only waits, and when a quest's
 * code first appears it loads and starts the body (otomo_body.prx, the same folder PSP/PLUGINS/otomo/) from the memory stick. So a
 * PPSSPP state saved in the village / the hall carries only this loader (no network, no body in it), and every quest
 * runs the body as it is on the memory stick now: the body can change without a new state, and a real PSP loads it
 * the same way. Its progress is a status block found by the magic "OTOMOBT1". */
#include <pspkernel.h>
#include <psputils.h>
#include "../otomo_body/layout.h"

PSP_MODULE_INFO("otomo_boot", PSP_MODULE_USER, 1, 0);

#define BODY "ms0:/PSP/PLUGINS/otomo/otomo_body.prx"   /* one folder: this loader's plugin.ini, the body, its cfg */

typedef struct {
    char magic[8];                  /* "OTOMOBT1" */
    volatile int step;              /* 1 waiting, 2 quest code seen, 3 body loaded, 4 body started */
    volatile int err;               /* the load / start error, if any */
} Status;

Status g_boot = { {'O', 'T', 'O', 'M', 'O', 'B', 'T', '1'}, 0, 0 };

#define U32(a) (*(volatile unsigned int *)(a))
#define EMUHACK(w) (((w) & 0xFC000000u) == 0x68000000u)

static int quest_code(void) {      /* the enter-behaviour routine is there (the original, maybe under a JIT marker) */
    unsigned int a = U32(ENTER_BEHAVIOUR), b = U32(ENTER_BEHAVIOUR + 4);
    return b == ENTER_ORIGINAL_1 && (a == ENTER_ORIGINAL_0 || EMUHACK(a));
}

static int boot_thread(SceSize args, void *argp) {
    g_boot.step = 1;
    sceKernelDelayThread(15 * 1000 * 1000);         /* let the game boot first */
    while (!quest_code()) sceKernelDelayThread(500 * 1000);
    g_boot.step = 2;
    SceUID m = sceKernelLoadModule(BODY, 0, NULL);
    if (m < 0) { g_boot.err = m; return 0; }
    g_boot.step = 3;
    int status = 0, go = 1;                         /* args = 1: the body starts at once (no boot wait) */
    int r = sceKernelStartModule(m, sizeof(go), &go, &status, NULL);
    if (r < 0) { g_boot.err = r; return 0; }
    g_boot.step = 4;
    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th = sceKernelCreateThread("otomo_boot", boot_thread, 0x30, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
