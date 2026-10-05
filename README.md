# Otomo — a brain for your Felyne comrade

*Otomo* (オトモ) is what the game calls its comrades: the companion. This is a small neural **brain** for the Felyne
comrade of *Monster Hunter Freedom Unite*. It rides a comrade of your own game,
as the game made it, and learns its own habits from the way **you** play: when the game's cat would follow you, attack,
gather, play a flute or set a trap, the brain may choose differently — within what that cat is allowed to do — and it
remembers how each choice turned out for the team. It starts as a novice: on day one it does what the game's cat would
do.

Two parts:

| Part | Where | What it does |
|---|---|---|
| **The body** | a plugin inside the game (`psp/`) | reads what the cat sees (its state, its hunter, the monsters, the game's proposals, hits, traps), sends it to the home, applies the brain's answers. It never changes the cat itself: name, temperament, skills, level and training stay the game's. |
| **The home** | an ESP32-C3 board on your Wi-Fi (`esp32c3/`) | the brain: decides, learns, keeps a diary of its habits and mistakes, and serves a small site to watch it think and to attach / detach it. |

They talk over UDP (port 7777): see [docs/PROTOCOL.md](docs/PROTOCOL.md).

## What you need

- *Monster Hunter Freedom Unite*, **EU version, ULES01213 v1.01** (your own copy), on **PPSSPP** with plugins enabled.
  Other versions use other addresses and are not supported. A real PSP is not tested.
- An **ESP32-C3** board (developed on a "SuperMini") on the same network as the PC running PPSSPP.
- To build: [pspdev](https://github.com/pspdev/pspdev) (the plugin) and [PlatformIO](https://platformio.org/) (the board).

## Build and install

**The plugin** (in a shell with pspdev):

```bash
cd psp
./build.sh otomo_boot
./build.sh otomo_body
```

Copy into PPSSPP's memory stick, folder `PSP/PLUGINS/otomo/`:

- `otomo_boot/plugin.ini` and `otomo_boot/otomo_boot.prx` (the loader the game boots with),
- `otomo_body/otomo_body.prx` (the body; the loader starts it at your first quest),
- `otomo_body.cfg`: one line, the board's address and port, e.g. `10.0.0.42 7777` (see `psp/otomo_body.cfg.example`).

In PPSSPP's settings enable plugins (`EnablePlugins = True`, `LoadPlugins = True` in `ppsspp.ini`).

**The board:**

```bash
cd esp32c3/otomo_link
pio run -t upload       # the firmware
pio run -t uploadfs     # the file system: the site and the game's lists - no cat yet
```

Wi-Fi: copy `include/wifi_secrets.h.example` to `include/wifi_secrets.h` and fill it in, or leave it out and the board
starts WPS (press your router's WPS button). The serial console (`pio device monitor`) prints the board's address.

## Use

1. Open the board's address in a browser: the site. The home is empty.
2. Press **New brain**.
3. In the game, hire / equip the comrade you want (at the Comrade Board) and take it on a quest: at the quest's start
   that comrade receives the brain. From then on the brain rides only that cat.
4. Press **Attach** on the site during a quest: the brain decides. **Detach** gives the game's own cat back. If the
   board is switched off or the Wi-Fi drops, the game's cat takes over by itself within two seconds.
5. Train the comrade at the board as usual: its level, stats and skills grow by the game's rules; the site keeps that
   growth, and the brain's habits, diary and quests.

## Safety

- The body writes only its own memory block and a few hooks; the comrade's data and the save are never written.
- Every answer of the brain is one the game's cat could give in that moment; outside them the game decides.
- Without a fresh answer for 2 seconds the game's cat decides again (a lease on the quest timer).

## Status

A research project. It works on PPSSPP (EU v1.01), solo quests. Co-op and real hardware are not tested. Not affiliated
with Capcom; *Monster Hunter* is a trademark of Capcom.
