# M5Stack Water Reminder

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A battery-friendly hydration reminder for the **M5StickC Plus** (ESP32-PICO + AXP192). Wakes from deep sleep at a configurable interval, runs a 3-minute alert with buzzer + LED + animated water glass, and goes straight back to deep sleep when you press A (drank it), press B (skip this one), or 3 minutes elapse.

Between reminders the device is fully asleep — pressing **A** (front) or **Power** (left side) wakes it into an interactive UI with Home / Stats / Battery pages plus a settings menu. Power-button presses also put the device back to sleep on demand. Flip face-down to nap the screen; flip face-up to wake it.

---

## Features

- **Scheduled reminders** — every 5 min, 10 min, 15 min, 30 min, 1 h, 90 min, 2 h, 3 h or 4 h (configurable). Default: 1 hour.
- **3-minute alert** — buzz rhythm + visual pulse every 30 s, LED blink throughout (1 Hz idle, 5 Hz during the buzz). Buzz rhythm (`single`, `triple`, `long`, `sos`, `melody`), buzz length (1–10 s per pulse), and volume (0–10) are all configurable from the menu.
- **Quiet hours** — silently skip reminders during a configurable window (default 00:00 → 09:00). Wraparound supported.
- **Daily glass counter** — counts A-presses, rolls over at midnight (RTC date change).
- **"Last drink X min ago"** on the Home page.
- **Battery page** — percentage, voltage, current, charge state.
- **Set the RTC** from the device itself (hour and minute items in the settings menu).
- **Deep sleep** between reminders and after 30 s of idle in interactive mode — sips current from the AXP192 battery rail. Idle-deep-sleep can be disabled via the `auto-sleep` setting if you want the device to stay on (e.g. on the desk while charging); the reminder still fires in-place when its scheduled minute arrives.
- **Power button toggles deep sleep** — short-press the left-side Power (PEK) button to sleep on demand; press it again to wake even if the device is already in deep sleep.
- **Face-down nap** — flip the device face-down and the screen dims and animations pause; flip it back over and it wakes instantly. Skipped during the reminder alert so you can't silence a nag by laying it down.
- **Persistent settings** in NVS (`Preferences` namespace `water`).

---

## Hardware

- **Board:** M5StickC Plus (ESP32-PICO-D4, AXP192 PMIC, BM8563 RTC, MPU6886 IMU, 135×240 ST7789v2 display)
- **Buttons used:** BtnA (GPIO 37, front), BtnB (GPIO 39, side), Power / PEK (left side, via AXP192)
- **Buzzer:** built-in (driven through `M5.Beep`)
- **LED:** GPIO 10 (active-low)

Wake sources used in deep sleep:

- `esp_sleep_enable_timer_wakeup` — fires the scheduled reminder.
- `esp_sleep_enable_ext0_wakeup` on `GPIO 35` (shared AXP IRQ + MPU6886 INT line) trigger-LOW — fires on a short-press of the Power (PEK) button **and** on MPU6886 wake-on-motion. The AXP192 PEK short-press IRQ is enabled in register `0x42` bit 1 (`powerInit()`); wake-on-motion is armed via `M5.Imu.enableWakeOnMotion()` immediately before each deep sleep when `wake on lift` is enabled.
- `esp_sleep_enable_ext1_wakeup` on `{GPIO 37}` with `ESP_EXT1_WAKEUP_ALL_LOW` — fires on a BtnA press. (BtnB is intentionally not a wake source: ESP32 ext1 only supports `ALL_LOW` for active-low pins, so a two-pin mask would require both buttons pressed simultaneously.)

---

## Controls

### Interactive mode (after a button or power-on wake)

| Action                       | Effect                                                  |
| ---------------------------- | ------------------------------------------------------- |
| **Short press A**            | Next page: Home → Stats → Battery → Home                |
| **Hold A (≥ 0.6 s)**         | Open / close settings menu                              |
| **B**                        | (No action on regular pages)                            |
| **Power (left, short)**      | Deep sleep on demand — press again to wake              |
| **Power (left, ~6 s hold)**  | Hard power off (AXP shutdown — survives via cold boot)  |
| **Flip face-down**           | Nap: dim screen + pause UI                              |
| **Flip face-up**             | Un-nap: restore brightness and resume                   |
| Idle 30 s (face-up)          | Deep sleep until next reminder                          |

### Inside the settings menu

| Action            | Effect                                             |
| ----------------- | -------------------------------------------------- |
| **Short press A** | Move cursor to next item                           |
| **B**             | Change selected value (toggle / cycle / increment) |
| **Hold A**        | Save and exit menu                                 |

The menu is split into two pages — **REMINDER** (when/how the reminder fires) and **DEVICE** (display/sound/system). Each page ends with `next page` (B flips to the other page) and `exit` (closes the menu).

**REMINDER page:** `interval`, `start hr`, `start min`, `quiet from`, `quiet to`, `rhythm`, `buzz len`, `volume`, `preview`, `next page`, `exit`.

**DEVICE page:** `bright`, `sound`, `led`, `auto-sleep`, `wake on lift`, `set hour`, `set min`, `shutdown`, `next page`, `exit`.

- **`interval`** — 5, 10, 15, 30, 60, 90, 120, 180, 240 minutes.
- **`start hr` / `start min`** — anchor time-of-day for the schedule. Reminders fire at `start_hr`:`start_min`, then every `interval` after that, **aligned to the wall clock — not offset by boot time**. So `start hr = 09`, `start min = 00`, `interval = 60` → reminders at 09:00, 10:00, 11:00, etc. every day, regardless of when the device was last powered on. With `interval = 30` you'd get 09:00, 09:30, 10:00, … Default 09:00. Minutes step in 5-min increments.
- **`sound`** — master mute. `off` silences every tone (buzz, button beep, ack chirp).
- **`volume`** — buzzer level 0–10 (0 = silent, 10 = loudest). Pressing B previews the new level with a short tone so you can dial it in.
- **`rhythm`** — buzz pattern played at each pulse. Choices:
  - `single` — one short chirp
  - `triple` — three quick chirps
  - `long` — one extended tone
  - `sos` — Morse ... --- ...
  - `melody` — three ascending notes
  Pressing B previews the full pattern.
- **`buzz len`** — how long the rhythm plays at each pulse trigger (1, 2, 3, 5, or 10 s). The pattern loops until the budget is up. Default 2 s. Pulses fire every 30 s for the whole 3-minute alert.
- **`auto-sleep`** — `on` (default): the device deep-sleeps after 30 s of idle and after a reminder. `off`: stays awake indefinitely in interactive mode; reminders still fire on schedule. Sleep on demand by short-pressing the Power button.
- **`wake on lift`** — `on` (default): the MPU6886 is armed for wake-on-motion before each deep sleep, so picking the device up wakes it instantly (threshold ≈ 156 mg at ±16 G — high enough to ignore desk vibration). `off`: only the Power button, BtnA, or the scheduled reminder timer wake the device.

### During a reminder

| Action                  | Effect                                                                              |
| ----------------------- | ----------------------------------------------------------------------------------- |
| **A**                   | Acknowledge ("Drank it!") → increment counter → go back to deep sleep               |
| **B**                   | Skip / pass — no glass counted, "skipped" screen → back to deep sleep               |
| **Power (left, short)** | Silently dismiss — no glass counted, straight back to deep sleep                    |
| Wait 3 min              | Silent timeout → "snoozed" → back to deep sleep                                     |
| (Face-down)             | Ignored — the alert is meant to nag you; laying the device down can't silence it    |

> **B (skip) vs. Power (dismiss):** both pass on the drink without counting a glass and both let the schedule advance normally — the difference is feedback. **B** gives an explicit acknowledgement (a short "bo-boop" blip and a *skipped* screen) for when you consciously decide not to drink right now; **Power** is the silent "shut up and let me work" escape. Neither touches the daily counter or the "last drink" timestamp.
>
> The next scheduled wake is computed from your interval setting and quiet-hour window — if the next wake would land inside quiet hours, the device sleeps until quiet hours end instead.

---

## Build & flash

PlatformIO is required. From the project root:

```sh
# Compile only
pio run

# Compile and flash over USB
pio run -t upload

# Full wipe (clears NVS / settings) then flash
pio run -t erase && pio run -t upload

# Live serial monitor (115200 baud)
pio device monitor
```

The `platformio.ini` is pinned to `board = m5stick-c` (the M5StickC Plus reuses the same board definition).

---

## First-boot setup

1. Power on the device with the **power button** (left side, short press).
2. The splash screen shows briefly, then drops to the Home page.
3. Press A to cycle to the Stats page — the clock reads `00:00` because the RTC hasn't been set yet.
4. **Hold A** to open the settings menu. Select `set hour` and press B until the hour is correct, then `set min` and press B to dial in the minutes. The first time you change the clock the date is bumped to 2024-01-01 so quiet hours start working.
5. Optionally set `interval`, `quiet from`, `quiet to`, `bright`, `sound`, `led`.
6. **Hold A** to save and exit, or just leave it — it auto-saves and deep-sleeps after 30 s.

> Until the RTC year is ≥ 2024, **quiet hours are disabled** so a freshly-powered device doesn't silently sleep through every reminder because it thinks it's 00:00 of the year 2000.

---

## Project layout

```text
src/
  main.cpp        boot flow + interactive loop (page cycling, menu, face-down nap, power-button sleep, idle deep-sleep)
  settings.h/cpp  WaterSettings struct, NVS persistence, interval choices
  power.h/cpp     wake-reason detection, RTC math, AXP PEK IRQ setup, deepSleepFor / deepSleepUntilButton
  ui.h/cpp        sprite-rendered pages, settings menu, animated glass, ack/skipped/timeout screens
  reminder.h/cpp  blocking 3-minute alert state machine (buzz, LED, animation, A→ack, B→skip, Power→dismiss)
platformio.ini    board = m5stick-c, lib = m5stack/M5StickCPlus
```

Boot flow (`main.cpp`):

```text
setup()
 ├─ M5.begin, IMU init, load settings, init UI, powerInit (enable AXP PEK IRQ)
 ├─ switch (wakeReason())
 │    ├─ WAKE_TIMER     → if quiet → re-sleep; else runReminder() → deep sleep
 │    └─ WAKE_BUTTON / WAKE_POWER_ON → fall through to interactive loop
loop()
 ├─ IMU poll → face-down hysteresis → enter/leave nap (dim + freeze UI)
 ├─ AXP PEK short-press poll → deepSleepFor(secondsUntilNextReminder())
 ├─ (if not napping) poll buttons (short A / long A / B), update menu or page index
 ├─ (if not napping) draw current page or menu to sprite, push
 └─ (if not napping) if idle > 30 s → deepSleepFor(secondsUntilNextReminder())
```

---

## Settings reference

Stored in NVS namespace `water` via the Arduino `Preferences` library:

| Key    | Type   | Default | Meaning                                                                                        |
| ------ | ------ | ------- | ---------------------------------------------------------------------------------------------- |
| `intv` | uint16 | 60      | Reminder interval, minutes                                                                     |
| `anh`  | uint8  | 9       | Schedule anchor hour (0–23). Reminders fire at `anh:anm` + N·interval, aligned to wall clock   |
| `anm`  | uint8  | 0       | Schedule anchor minute (0–55, 5-min steps via menu)                                            |
| `qs`   | uint8  | 0       | Quiet hours start (0–23)                                                                       |
| `qe`   | uint8  | 9       | Quiet hours end (0–23). `qs == qe` disables quiet hours                                        |
| `br`   | uint8  | 2       | Screen brightness 0–4 (→ AXP `ScreenBreath` 20–100)                                            |
| `snd`  | bool   | true    | Buzzer enabled                                                                                 |
| `led`  | bool   | true    | LED enabled                                                                                    |
| `asl`  | bool   | true    | Auto-sleep on idle (30 s). `false` = stay awake until power-button press / face-down nap       |
| `wom`  | bool   | true    | Wake-on-motion via MPU6886. `true` arms the IMU before deep sleep so pick-up wakes the device  |
| `vol`  | uint8  | 8       | Buzzer volume 0–10. 0 = silent (overrides `snd`-on for the buzzer)                             |
| `pr`   | uint8  | 1       | Pulse rhythm index: 0=single, 1=triple, 2=long, 3=sos, 4=melody                                |
| `plen` | uint8  | 2       | Pulse length in seconds. Allowed: 1, 2, 3, 5, 10                                               |
| `ldnk` | uint32 | 0       | RTC-derived wall-minute of last A-press                                                        |
| `next` | uint32 | 0       | Scheduled next-reminder wall-minute (drives the home countdown). 0 = unscheduled / RTC not set |
| `gtod` | uint16 | 0       | Glasses counted today                                                                          |
| `lday` | uint8  | 0       | RTC day-of-month for daily rollover detection                                                  |

To clear all settings (also re-runs the splash on first boot): `pio run -t erase && pio run -t upload`.

---

## Notes

- The reminder loop is blocking by design — it runs inside `setup()` after a `WAKE_TIMER` wake, then deep-sleeps. No FreeRTOS task is involved.
- `secondsUntilNextReminder()` (`power.cpp`) handles both the interval math and the quiet-hour deferral. The `setup()` quiet-hour check is a safety net for the case where the user changes quiet hours between sleeps.
- The animated water glass is drawn with `TFT_eSprite` primitives — no GIF assets, no extra libraries.
- M5StickC Plus deep-sleep current is small but **not zero** — the AXP192 itself draws ~50 µA. With a fully charged 120 mAh cell that's still weeks of standby.

---

## License

Copyright (c) 2026 Ramy Ezzat.

Released under the [MIT License](LICENSE) — you're free to use, modify and redistribute under its terms.

Third-party software linked at build time keeps its own licenses:

- [M5StickCPlus](https://github.com/m5stack/M5StickC-Plus) — MIT (M5Stack)
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32) — LGPL v2.1
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (bundled with M5StickCPlus) — see its FreeBSD-style license
