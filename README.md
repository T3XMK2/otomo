# Otomo <sup>オトモ</sup>

**Adaptive behavior for your Felyne comrade in Monster Hunter Freedom Unite.**

[Installation](docs/SETUP.md) · [Usage](#usage) · [Protocol](docs/PROTOCOL.md) · [Issues](https://github.com/T3XMK2/otomo/issues)

---

Otomo gives your in-game comrade a neural brain on an **ESP32-C3**. A plugin observes the hunt, the board chooses how to respond, and the brain learns from the consequences for you and your Felyne.

It begins with a preference for the game's own decisions. As you hunt together, its choices can develop into habits: when to fight, stay close, gather or offer support. Its name, temperament, skills and progression continue to follow the game's rules.

> **Current target:** PPSSPP on PC · MHFU EU `ULES01213 v1.01` · solo quests.  
> Otomo is experimental. Other game versions are unsupported; co-op and real PSP hardware are untested.

## What Otomo adds

**Experience that persists.** The brain and diary are stored on the board. Its history grows across quests, alongside records of the comrade's normal progression.

**Decisions you can inspect.** Open the board's web interface to explore its choices, learned habits and quest history, and to choose when the brain takes control.

**Learning within the game's rules.** The brain selects from allowed alternatives to a proposed action. The comrade's existing abilities define what it can do.

## Architecture

| In the game · **Body** | On the board · **Home** |
| :--- | :--- |
| A PSP plugin reads the hunt and the game's proposed behaviors, then applies the selected responses. | The ESP32-C3 interprets observations, runs the neural brain, learns and stores experience. |

The two components communicate over your local network using **UDP port 7777**. The board also serves the dashboard over **HTTP**, so you can access it from a browser. All neural computation runs on the ESP32-C3.

[Read the communication protocol →](docs/PROTOCOL.md)

## Installation

Prepare an **ESP32-C3**—development uses a SuperMini—and a PC running **PPSSPP** with your own copy of the supported game. Both devices must share a local network.

You will use [PlatformIO](https://platformio.org/) to flash the board and [pspdev](https://github.com/pspdev/pspdev) to build the plugins.

```bash
git clone https://github.com/T3XMK2/otomo.git
cd otomo
```

**[Open the setup guide →](docs/SETUP.md)**

The guide covers Wi-Fi configuration, firmware upload, plugin compilation, PPSSPP installation and troubleshooting.

## Usage

1. **Create a brain.** Visit `http://<board-ip>/`. Open **Home** and select **Give a brain to my next comrade**.
2. **Choose its comrade.** Equip your Felyne at the in-game Comrade Board and start a quest. The waiting brain becomes associated with that comrade.
3. **Start learning.** Select **Let &lt;name&gt; decide** in the dashboard and play. Use **Give control back** to return decisions to the game.

One board holds one active brain, associated with one Felyne. Equipping another comrade does not transfer it. The dashboard can archive a resident and bring it back into an empty home later.

## Control and persistence

- **Automatic fallback:** decisions carry a two-second lease measured on the quest timer. If fresh answers stop arriving, the game's behavior resumes. The lease is cleared between quests.
- **Game data stays with the game:** the plugin writes its own memory block and required hooks, without writing comrade data or the save file.
- **Experience is saved:** new learning is stored approximately once a minute and at quest end. Replacing the board's filesystem with `uploadfs` requires backing up existing memories first.

## Development

The project separates game-specific hooks from the brain and its web interface:

```text
psp/
  otomo_boot/     Plugin loader
  otomo_body/     Game hooks and communication
esp32c3/
  otomo_link/     Brain, learning, storage and web server
web/             Dashboard source
docs/            Setup and protocol documentation
```

[Report a bug](https://github.com/T3XMK2/otomo/issues) with your game version, PPSSPP version, board model and steps to reproduce it. Include relevant logs with Wi-Fi credentials removed.

---

[MIT License](LICENSE) · An independent project, not affiliated with or endorsed by Capcom. *Monster Hunter* is a trademark of Capcom.
