# Installation guide

[← Back to Otomo](../README.md)

Otomo has two components: the **home**, an ESP32-C3 that runs the brain, and the **body**, a plugin running inside the game.

## Prerequisites

- Your own copy of **Monster Hunter Freedom Unite EU, ULES01213 v1.01**.
- **PPSSPP on a PC**, with plugins enabled.
- An **ESP32-C3** and USB connection for flashing; development uses a SuperMini.
- A local network shared by the PC and board.
- [PlatformIO](https://platformio.org/), with `pio` available in your terminal.
- [pspdev](https://github.com/pspdev/pspdev) in a Bash environment such as WSL.

## 1. Get the source

```bash
git clone https://github.com/T3XMK2/otomo.git
cd otomo
```

## 2. Configure and flash the board

Connect the board over USB. From the repository root:

```bash
cd esp32c3/otomo_link
```

Copy `include/wifi_secrets.h.example` to `include/wifi_secrets.h` and enter your credentials:

```cpp
#define WIFI_SSID "your-network"
#define WIFI_PASS "your-password"
```

The credentials file is excluded from Git. Alternatively, omit it and press your router's WPS button when the board starts WPS. The firmware first tries a previously saved network.

Upload the firmware and the initial filesystem, which contains the dashboard and game metadata:

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

The serial console prints the board's IP address. Visit `http://<board-ip>/` in your browser, and keep the address for the plugin configuration.

> **Existing boards:** `uploadfs` replaces the filesystem image. The learned brain and diary are stored in that filesystem too. Back up existing memories before repeating this step.

## 3. Build the plugins

In your pspdev Bash environment, start from the repository root:

```bash
cd psp
./build.sh otomo_boot
./build.sh otomo_body
```

[`psp/build.sh`](../psp/build.sh) currently sets `PSPDEV=/root/pspdev`. Update this path before building if your toolchain is installed elsewhere.

## 4. Install in PPSSPP

Inside PPSSPP's memory stick directory, create:

```text
PSP/PLUGINS/otomo/
├── plugin.ini
├── otomo_boot.prx
├── otomo_body.prx
└── otomo_body.cfg
```

Copy `plugin.ini` and `otomo_boot.prx` from `psp/otomo_boot/`. Copy `otomo_body.prx` from `psp/otomo_body/`.

Create `otomo_body.cfg` using the [example](../psp/otomo_body.cfg.example). It contains your board's IPv4 address and UDP port on one line, separated by a space:

```text
10.0.0.42 7777
```

Replace `10.0.0.42` with the address printed by your board. Enable plugins in PPSSPP's settings. The corresponding entries in `ppsspp.ini` are:

```ini
EnablePlugins = True
LoadPlugins = True
```

The boot plugin loads with the game and starts the body plugin at your first quest.

## 5. Create the brain

1. Open the board's dashboard. In **Home**, select **Give a brain to my next comrade**.
2. Hire or equip your chosen Felyne at the in-game Comrade Board.
3. Take it on a quest. At the quest's start, the brain becomes associated with that comrade.
4. Press **Let &lt;name&gt; decide** to hand over control. Press **Give control back** to return it to the game.

The home recognizes its own comrade. Bringing a different Felyne does not transfer the brain.

## Experience and storage

New learning is saved approximately once a minute and at quest end. The dashboard tracks the comrade's recorded growth, habits, diary and quests.

Each home has one active resident. Through the dashboard, you can release it into an archive and bring it back into an empty home later.

## Troubleshooting

| Symptom | Check |
| :--- | :--- |
| Dashboard does not open | Read the current IP in `pio device monitor`; check the Wi-Fi connection and that your browser can reach the board. |
| Dashboard opens, but no game arrives | Verify the IP and port in `otomo_body.cfg`, the plugin files, PPSSPP plugin settings and the exact game version. Check that the local network permits UDP traffic on port 7777. |
| Brain is still waiting | Equip a comrade and start a new quest; association happens at the quest's start. |
| Brain will not control another Felyne | This is intentional: it is associated with its own comrade. |
| Plugin build cannot find the toolchain | Correct the `PSPDEV` path in `psp/build.sh`. |

See [Protocol](PROTOCOL.md) for transport details, or [report an issue](https://github.com/T3XMK2/otomo/issues) with your setup and reproduction steps. Do not include Wi-Fi credentials in logs or screenshots.
