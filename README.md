# Costean OS for Bitaxe Gamma 601/602

> [!WARNING]
> **For the Bitaxe Gamma 601 and 602 only (1x BM1370, ESP32-S3, 16 MB flash).** Do not flash it on any other board. Use the matching factory image (601 or 602).
>
> **Pre-release. Flash at your own risk.** The auto-tune searcher in 1.2.0-costean.2 is new and has **not yet been tested on hardware**. Auto-tune stays off by default. The 1.2.0-costean.1 crash fix was verified on a real Gamma 602 (Wi-Fi setup, joining the network and the web dashboard). **601 support is based on the shared upstream board code; we have not tested this firmware on a 601.** The pool split, Discord alerts and schedules were tested only against fake Stratum pools and demo data. Keep a recovery image handy: 1.2.0-costean.1, or the official ESP-Miner v2.15.4 factory image for your board (see [Recovery](#recover-to-official-esp-miner-v2154)).

Costean OS is a custom firmware for the Bitaxe Gamma 601 and 602. (A *costean*, pronounced COS-teen, is an old mining word for a trench dug to find the lode.) It is a source fork of the official [bitaxeorg/ESP-Miner](https://github.com/bitaxeorg/ESP-Miner) **v2.15.4** (GPL-3.0) with a new web UI and extra firmware features written in C. The 601 and 602 are the same Gamma family (one BM1370, TPS546, EMC2101); the factory images differ only in the NVS board version. `esp-miner.bin` and `www.bin` are shared.

Version: **1.2.0-costean.2** (pre-release) (Costean OS 1.2 for Bitaxe Gamma 601/602, plus the auto-tune searcher), built from **bitaxeorg/ESP-Miner v2.15.4** with **ESP-IDF v6.0.3**.

![Dashboard](docs/screenshots/web-01-dashboard.png)

## 1.2.0-costean.2: auto-tune searcher

On top of 1.2.0-costean.1. **The tuner is new and has not yet been tested on hardware.** Auto-tune stays off until you turn it on. Ideas only from AxeForge (GPL-3.0); the searcher, UI and OLED line are original Costean code. 601 support follows the shared upstream Gamma board table; we have not run this build on a 601.

- **Auto-tune searcher, off by default** (turning it on needs you to type `AUTO-TUNE`). It sits beside the safety governor, which still throttles first. The default goal is **Best efficiency**: it never raises voltage, climbs in 25 MHz steps from the active profile while J/TH keeps falling (2-minute settle + 5-minute window), then trims voltage in 10 mV steps and keeps a 10 mV margin. Also available: **Most hashrate**, and **Most hashrate under a power cap** (the cap cannot exceed the PSU rating; the search stops 5 % early). A locked point is saved and rechecked every 10 minutes. Gamma limits stay in firmware: 1000–1300 mV, chip 68 °C and 27 W by default (never above 1000 MHz). Errors, rejects, missing temperatures, overheating or a governor throttle roll the search back. Stock overheat protection still turns auto-tune off and switches to Manual.
- **Profiles & Limits strip** and a dashboard chip show SEARCHING / TRIMMING / LOCKED / WATCHING, the trial point, live J/TH, progress and a step log, plus Restart search.
- **OLED:** one line `LOCK 575/1100` or `TUNE 550/1150` (HASH bottom line, or the HEAT header tag).
- **PSRAM/flash rule** from the costean.1 crash still applies: the searcher never maps flash; the lock is saved only from the Costean task (internal-RAM stack). A host test covers the tuner.

## 1.2.0-costean.1: a fresh start

Costean OS for the Gamma 602 starts its numbering again at **1.2.0-costean.1**. Earlier test builds were withdrawn and are no longer offered. If you have an older file called `1.2.0-costean.1`, it is an early test build: use this release instead and check the SHA-256 against `SHA256SUMS`.

**Tested on a real Gamma 602:** Wi-Fi setup from a phone, joining the home network and the web dashboard. Still a pre-release: mining features (pool split, alerts, schedules, auto-tune) were tested against fake pools and demo data.

- **Fixed: the miner restarted in a loop as soon as the web UI connected.** The live dashboard connection built the system info in a task whose stack is in PSRAM, and that code read the boot slot from flash. ESP-IDF stops the firmware when flash is mapped from such a task, so the miner crashed every time the web UI was open (on the phone during setup, or in a browser once it had an IP address). The boot slot is now read once at start-up and after an update, and a host test guards against flash access from that code.
- **Wi-Fi join with the setup hotspot off.** With a network saved, the miner joins in station mode only, so no phone or captive-portal traffic competes with the router login. If the network cannot be joined for 90 s, the `Bitaxe_XXXX` hotspot comes back so you can fix the password, and joining keeps retrying in the background. With no network saved, the hotspot starts right away.
- **Phone Wi-Fi setup.** The setup page opens by itself on the hotspot, scans and lists the networks it can hear (strongest first), and has a field for hidden networks. A fresh miner starts with no Wi-Fi saved (no `myssid` placeholder).
- **Clear join and restart reasons.** The OLED and setup page say *Wrong password?*, *Network not found*, *Security type not supported*, *Router refused the miner* (error 202, often a WPA3 handshake problem), *Weak signal* or *No IP address from router*. The miner remembers why its last run ended (crash, watchdog, brownout, normal restart) and what it was doing.
- **Boot-loop guard and safe mode.** Three unexpected resets in a row, each within 2 minutes of starting, switch on safe mode: Wi-Fi and the web UI only, the ASIC stays off. Press **Restart normally** in the web UI to leave it.
- **Crash dump.** A crash is saved in the 64 KB `coredump` flash partition and can be downloaded at `http://YOUR-BITAXE-IP/api/system/coredump` (404 when none is saved). Please attach it to a bug report.
- **Checked updates.** Uploads are checked before anything is written (a firmware file in the www box is refused, and so on), and the Update & Recovery page reads the version back after the restart.
- **Readable OLED pages** (white on black: HASH, HEAT, SHARES, POOLS), live **Screen colors** setting, **editable profiles** with Custom 1 and Custom 2, settings that stick, 400–1000 MHz / 1000–1300 mV tuning and a power-supply setting.

![Setup: network list](docs/screenshots/setup-01-network-list.png)
![Setup: join failed, with the reason](docs/screenshots/setup-05-join-failed.png)
![Safe mode banner](docs/screenshots/safe-mode-dashboard.png)

- **Recovering a miner that keeps restarting:** flash `esp-miner-factory-602-v1.2.0-costean.1.bin` at **0x0** over USB (see Flashing), or the official ESP-Miner factory image (see [Recovery](#recover-to-official-esp-miner-v2154)). This resets all settings. The first boot after a factory flash runs the built-in self-test; if it fails, **hold the BOOT button for 2 seconds** to cancel it.

## What is new in 1.2

- **"Lode" look** on the web UI (graphite, copper and teal, diamond logo) and new 128×32 OLED pages: HASH, HEAT, SHARES, POOLS, with the active profile on the top line (THROTTLE when the governor is holding back, FAILOVER when a split pool is down, a clock icon when a schedule set the profile).
- **Profiles: Eco, Stock, Turbo, Manual.** Manual is the default and keeps your saved frequency / voltage, so nothing changes after the update until you pick another profile. Turbo needs you to type `TURBO`.
- **Protective limits that throttle instead of crashing**: chip (default 68 °C), voltage regulator (90 °C) and board power (90 % of the power supply you pick, 27 W on the default 30 W supply). Over a limit the clock steps down 25 MHz every 20 s (never below 400 MHz) and climbs back 25 MHz every 2 min once there is room. Lower clocks are applied before lower voltage. The stock overheat protection is unchanged and stays on underneath; if it ever trips, Costean OS switches to Manual and turns auto-tune off.
- **Auto-tune, off by default**: the 1.2.0-costean.2 searcher (Best efficiency by default, never raises voltage; also Most hashrate and hashrate under a power cap). 25 MHz steps, 2-minute settle + 5-minute window, then voltage trim in 10 mV steps with a 10 mV margin, lock saved and rechecked every 10 minutes. Limits still throttle first. Turning it on needs you to type `AUTO-TUNE`.
- **Schedules**: up to 8 weekly windows that switch the profile and/or the OLED (normal, dim, off). Built-in time zone and daylight time rules (default America/New_York). Nothing is applied until the miner has network time (SNTP). Default: the screen dims 22:00–07:00. A scheduled "off" screen still wakes for a block, overheat or identify.
- **History that survives a reboot**: 48 h of 15-minute points (hashrate, power, chip and VR temperature, J/TH, throttle / failover / schedule marks) in flash, saved every 30 min and before a restart.
- **Multi-pool split with failover**: up to 4 weighted pools (percentages that add up to 100) plus any number of 0 % backup pools. Jobs are shared by smooth weighted round robin; a pool that disconnects or has no work drops out at once and its share moves to the healthy pools; it must stay up 30 s before it rejoins. If every weighted pool is down the backups take the work. Per-pool job share, accepted / rejected shares and pool credit (accepted difficulty over 10 min). Changing weights applies live; turning split on or adding a pool needs a restart. V1 only.
- **Kept from 2.15.4-costean.1**: Discord alerts (block found, best difficulty, offline, overheat, periodic status; webhook never returned in full) and the Block Found page.
- New API: `GET /api/costean`, `GET /api/costean/history`, `PATCH /api/costean`. `/api/system/info` adds `poolSplitFailover`, `poolSplitRestartNeeded` and per-pool `healthy`, `backup`, `jobShare`, `creditedGhs`.

### Profiles on the Gamma 601/602

One BM1370 with 2040 small cores, so expected hashrate is about MHz × 2.04 GH/s. Power figures are estimates; every chip and cooler differs.

| Profile | Frequency / core voltage | Expected | Why |
|---|---|---|---|
| Eco | 425 MHz / 1100 mV | ≈ 0.87 TH/s, ≈ 13–14 W | About 19 % less clock and 50 mV less than stock: cooler, quieter, usually the best J/TH. |
| Stock | 525 MHz / 1150 mV | ≈ 1.07 TH/s, ≈ 16–18 W | Exactly the Bitaxe factory point. |
| Turbo | 575 MHz / 1180 mV | ≈ 1.17 TH/s, ≈ 19–21 W | One modest 50 MHz step and +30 mV, well under the 27 W default power limit and still throttled by the limits. |
| Manual (default) | your Mining & Tuning values | — | Nothing changes after the update. |

These mirror the NerdOctAxe 1.2 profiles in proportion (Eco well under stock, Turbo one step above). Limit ranges: chip 45–73 °C (always 2 °C under the stock 75 °C protection), VR 50–103 °C (under the stock 105 °C), power 10 W up to your power supply rating (10–150 W).

### Unchanged from upstream

- Stock frequency / voltage remains **525 MHz / 1150 mV**; fan control and the stock thermal protection (75 °C chip / 105 °C VR) are unchanged. The Costean Mining & Tuning page allows 400–1000 MHz / 1000–1300 mV without the `?oc` switch.
- Default pool is the official factory default: **public-pool.io:3333**, fallback **solo.ckpool.org:3333**. There is **no Costean dev fee** and no new hardcoded pool addresses.

More screenshots (desktop, mobile and every OLED screen, all demo data) are in [`docs/screenshots/`](docs/screenshots/).

## Downloads

Get the files from the [Releases](../../releases) page.

| File | What it is |
|------|------------|
| `esp-miner-factory-601-v1.2.0-costean.2.bin` | Full factory image for a **601** (bootloader, partition table, official 601 NVS defaults, firmware, otadata). Flash at **0x0**. **This wipes all settings.** |
| `esp-miner-factory-601-v1.2.0-costean.2.zip` | The same 601 factory image, zipped. |
| `esp-miner-factory-602-v1.2.0-costean.2.bin` | Full factory image for a **602**. Flash at **0x0**. **This wipes all settings.** |
| `esp-miner-factory-602-v1.2.0-costean.2.zip` | The same 602 factory image, zipped. |
| `esp-miner.bin` | OTA app image, shared by 601 and 602. Keeps your settings (including the board version). The Costean web UI is built in. |
| `www.bin` | Optional SPIFFS web UI partition (3 MB), only for the custom-WWW option. Not needed for normal installs. |
| `SHA256SUMS` | SHA-256 of each file. Check it with `sha256sum -c SHA256SUMS`. |

## Flashing

Flash the **601** factory image on a 601, and the **602** factory image on a 602. Do not flash a 401 or other family image onto a Gamma, or a Gamma image onto another family. The OTA files (`esp-miner.bin`, `www.bin`) are the same for 601 and 602.

### Option A: factory image at 0x0 (clean install, wipes settings)

Hold the BOOT button if the board is not already in download mode. Replace `PORT` with your serial device (for example `/dev/ttyACM0` or `COM3`).

```bash
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 esp-miner-factory-602-v1.2.0-costean.2.bin   # use -601- on a 601
```

You can also use [esptool-js](https://espressif.github.io/esptool-js/) and flash the factory file at address `0x0`.

After a factory flash the miner starts its setup hotspot (`Bitaxe_XXXX`) (once a network is saved, the hotspot stays off while it is being joined and comes back after 90 s without Wi-Fi). Join it with your phone; the setup page opens by itself (if not, open `http://192.168.4.1`). Tap your Wi-Fi, enter the password and save. Then set your pool again. The default pool is the official ESP-Miner default (public-pool.io), so set your own pool right away.

### Option B: OTA update (keeps settings)

Upload `esp-miner.bin` on the miner's update page (AxeOS **Update** page, or Costean **Update & Recovery**), or:

```bash
curl -X POST --data-binary @esp-miner.bin http://YOUR-BITAXE-IP/api/system/OTA
```

Optional custom web UI only:

```bash
curl -X POST --data-binary @www.bin http://YOUR-BITAXE-IP/api/system/OTAWWW
```

After flashing, check that hashrate, temperatures and fan look normal before you turn on the pool split.

## Recover to official ESP-Miner v2.15.4

If Costean OS misbehaves, flash a factory image at `0x0` over USB: 1.2.0-costean.1 (602 factory, or the shared `esp-miner.bin` over OTA to keep settings), or the official ESP-Miner v2.15.4 factory image for **your** board (601 or 602) below. A factory flash wipes all settings.

Costean OS does not re-host the official binaries. Download them from the official release: <https://github.com/bitaxeorg/ESP-Miner/releases/tag/v2.15.4>

- Factory image for 601 (flash at `0x0`, wipes settings): [`esp-miner-factory-601-v2.15.4.bin`](https://github.com/bitaxeorg/ESP-Miner/releases/download/v2.15.4/esp-miner-factory-601-v2.15.4.bin)
  SHA-256 (601) `d6307ce762d3fad529b705ad9c3b8fae96b66bd35125585b006f132c9de10184`
- Factory image for 602 (flash at `0x0`, wipes settings): [`esp-miner-factory-602-v2.15.4.bin`](https://github.com/bitaxeorg/ESP-Miner/releases/download/v2.15.4/esp-miner-factory-602-v2.15.4.bin)
  SHA-256 (602) `0be9a5270f36136335a9fe535ffa74c6f8195efdb56d26b53ac65403c90fd86d`
- OTA image (keeps settings): [`esp-miner.bin`](https://github.com/bitaxeorg/ESP-Miner/releases/download/v2.15.4/esp-miner.bin)
  SHA-256 `b28cd642025217a333f569fd66d433e65d5caa02c5be1a31fde960f7f32631bd`

## Building from source

```bash
git clone --recursive <this repo>
cd <this repo>
# ESP-IDF v6.0.3 environment, Node.js 20+ for the web UI
idf.py build
```

The web UI source is in `main/http_server/costean-ui` (Svelte + Vite) and is built and embedded by the normal `idf.py build`.

Tests (no hardware needed):

```bash
test/costean/run.sh                       # governor, scheduler, pool split, searcher, Gamma profiles, OLED renderer, PSRAM/flash guard
gcc -Imain -o /tmp/t test/host/test_pool_split_core.c && /tmp/t
python3 test/host/mock_stratum_split.py   # fake Stratum V1 pools
cd main/http_server/costean-ui && npm test && npm run build && node e2e/shots.mjs && node e2e/setup-shots.mjs && node e2e/persist.mjs   # unit, build, screenshots, click-through, save → restart → persist
python3 tools/lode_preview.py /tmp/oled   # render every OLED screen to PNG (1:1, 4x and a contact sheet)
``` The original ESP-Miner README is kept in [`docs/ESP-MINER-UPSTREAM-README.md`](docs/ESP-MINER-UPSTREAM-README.md).

## Known limits

- **The auto-tune searcher has not been tested on hardware.** No device was flashed during the 1.2.0-costean.2 build. Host tests (unit tests, fake Stratum pools, a mock miner for the web UI) cover the new tuner. The 1.2.0-costean.1 crash fix was verified on a real Gamma 602. 601 support is based on the shared upstream board code; we have not tested this firmware on a 601.
- One BM1370: split is job sourcing across simultaneous V1 connections, not per-chip assignment. SV2 participants are rejected with an API / UI error.
- Turning split on, or changing which pools take part, needs a restart. Weight changes apply live.
- Schedules need network time; until SNTP syncs, no window is applied.
- High clocks (above about 700 MHz / 1200 mV) are your own risk: the 5 V barrel jack, the power supply and the TPS546 regulator's 25 A warning / 30 A fault current limits were not checked on hardware at 950 MHz / 1250 mV. If the board resets under load, lower the clock or check Diagnostics for the restart reason. The stock overheat protection may also lower the saved frequency if the chip reaches 75 °C.
- `blockFound` is a RAM counter (since last boot), matching upstream.
- Discord webhooks must be `https://discord.com/api/webhooks/…` (or discordapp.com). The URL is never logged or returned in full.
- Going back to 2.15.4-costean.1 or official v2.15.4 by OTA works; the Costean 1.2 settings live in their own NVS namespace (`costean`) and are ignored there. Schedules from 2.15.4-costean.1 are not carried over (the default night-dim schedule is used).

## Credits and license

Costean OS is based on [bitaxeorg/ESP-Miner](https://github.com/bitaxeorg/ESP-Miner) by the Bitaxe / OSMU contributors. All upstream history is kept in this repository. ESP-Miner is licensed under the GNU General Public License v3.0, and so is Costean OS; see [LICENSE](LICENSE). The Costean web UI is GPL-3.0-or-later.

This is an unofficial fork and is not affiliated with or endorsed by the Bitaxe project.
