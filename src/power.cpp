// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include "power.h"
#include "settings.h"
#include <M5StickCPlus.h>
#include <esp_sleep.h>

// M5StickC Plus pins used as deep-sleep wake sources:
//   BtnA  = GPIO 37 (front button, input-only, RTC pin, pulled-up, active-low)
//   AXP IRQ = GPIO 35 (open-drain, pulled-up, AXP192 pulls it LOW on PEK events)
// BtnB (GPIO 39) is deliberately NOT a wake source: ESP32 ext1 only supports
// ESP_EXT1_WAKEUP_ALL_LOW for active-low pins, which would require pressing
// BtnA + BtnB simultaneously. ext0 only supports one pin and is taken by AXP
// IRQ. Power button covers the second wake-from-sleep affordance.
static const gpio_num_t PIN_BTN_A   = GPIO_NUM_37;
static const gpio_num_t PIN_AXP_IRQ = GPIO_NUM_35;

WakeReason wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:     return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:      return WAKE_BUTTON;
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WAKE_POWER_ON;
    default:                         return WAKE_OTHER;
  }
}

bool rtcIsValid() {
  RTC_DateTypeDef dt; M5.Rtc.GetDate(&dt);
  return dt.Year >= 2024;
}

uint32_t rtcWallMinutes() {
  RTC_TimeTypeDef tm; RTC_DateTypeDef dt;
  M5.Rtc.GetTime(&tm);
  M5.Rtc.GetDate(&dt);
  // Days since an arbitrary fixed epoch (year 2000-01-01). Good enough for
  // monotonic "minutes since something" math; BM8563 holds year 2000..2099.
  int y = dt.Year - 2000;
  static const uint8_t DPM[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  int days = y * 365 + (y / 4);   // leap days for years 2000,04,08,...
  for (int m = 1; m < dt.Month; m++) {
    days += DPM[m - 1];
    if (m == 2 && (y % 4) == 0) days += 1;   // 2000-2099 simple leap rule
  }
  days += dt.Date - 1;
  return (uint32_t)days * 24UL * 60UL + (uint32_t)tm.Hours * 60UL + tm.Minutes;
}

void scheduleNextReminder() {
  if (!rtcIsValid()) {
    // No reliable now-time; clear the schedule. UI falls back to interval.
    if (settings().nextReminderMin != 0) {
      settings().nextReminderMin = 0;
      settingsSave();
    }
    return;
  }

  // Anchored interval: reminders fire at anchorHour:anchorMinute + N*interval
  // for N = 0, 1, 2, … aligned to the wall clock. Pick the smallest N that
  // puts the target strictly after now. This is what gives the user
  // "predictable hourly chimes" instead of "1 hour from boot".
  uint32_t nowMin = rtcWallMinutes();
  RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
  uint32_t todayMidnight = nowMin - (uint32_t)tm.Hours * 60UL - tm.Minutes;
  uint32_t anchorMinOfDay = (uint32_t)settings().anchorHour * 60UL + settings().anchorMinute;
  uint32_t anchor = todayMidnight + anchorMinOfDay;
  uint32_t interval = settings().intervalMin ? settings().intervalMin : 60;

  uint32_t target;
  if (anchor > nowMin) {
    // Today's anchor hasn't happened yet — that's the first occurrence.
    target = anchor;
  } else {
    // Advance N cycles past anchor until we land strictly after now.
    uint32_t cyclesElapsed = (nowMin - anchor) / interval;
    target = anchor + (cyclesElapsed + 1) * interval;
  }

  settings().nextReminderMin = target;
  settingsSave();
}

uint32_t minutesUntilNextReminder() {
  if (!rtcIsValid() || settings().nextReminderMin == 0) {
    return settings().intervalMin;
  }
  uint32_t now = rtcWallMinutes();
  if (settings().nextReminderMin <= now) return 0;
  return settings().nextReminderMin - now;
}

uint32_t secondsUntilNextReminder() {
  uint32_t base = (uint32_t)settings().intervalMin * 60UL;
  if (!rtcIsValid()) return base;

  RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);

  // Quiet hours dominate: defer to quiet-end regardless of the schedule.
  // The schedule is preserved untouched; on quiet-end wake we re-evaluate.
  if (inQuietHours(tm.Hours)) {
    uint8_t qe = settings().quietEndH;
    int curMin = tm.Hours * 60 + tm.Minutes;
    int endMin = qe * 60;
    int delta  = endMin - curMin;
    if (delta <= 0) delta += 24 * 60;
    return (uint32_t)delta * 60UL - tm.Seconds;
  }

  // Use the persisted schedule when available. Subtract the seconds-within-
  // minute so we wake right at the targeted minute (not up to 59s late).
  if (settings().nextReminderMin > 0) {
    uint32_t now = rtcWallMinutes();
    if (settings().nextReminderMin > now) {
      uint32_t deltaMin = settings().nextReminderMin - now;
      int32_t total = (int32_t)(deltaMin * 60) - (int32_t)tm.Seconds;
      if (total < 5) total = 5;     // floor: don't try to sleep ~0s
      return (uint32_t)total;
    }
    return 5;                        // schedule overdue — fire ASAP
  }

  return base;
}

void powerInit() {
  // Enable AXP192 PEK short-press IRQ so the AXP pulls its IRQ pin LOW on a
  // short-press event. Register 0x42 IRQ Enable 3, bit 1 = PEK short press.
  // AXP registers persist as long as battery is connected, so this is
  // effectively a one-time setup but writing it every boot is idempotent.
  M5.Axp.Write1Byte(0x42, 0x02);
  // Clear any pending PEK IRQ status so we don't immediately re-fire.
  M5.Axp.GetBtnPress();
}

uint8_t powerButtonPressed() {
  return M5.Axp.GetBtnPress();
}

static void clearPowerButtonIrq() {
  M5.Axp.GetBtnPress();
}

static void armWakeSources() {
  // ext0: AXP IRQ on GPIO 35, trigger LOW. Fires on power-button short press.
  esp_sleep_enable_ext0_wakeup(PIN_AXP_IRQ, 0);
  // ext1: BtnA. With a single-pin mask, ALL_LOW behaves the same as ext0 LOW.
  const uint64_t mask = (1ULL << PIN_BTN_A);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
}

static void shutdownPeripherals() {
  M5.Beep.mute();
  M5.Beep.end();
  M5.Axp.ScreenBreath(0);
  M5.Axp.SetLDO2(false);      // backlight rail off
}

static void armImuWakeOnMotion() {
  if (!settings().wakeOnMotion) return;
  // MPU6886 INT line is wired to GPIO 35 on M5StickC Plus — the same line
  // the AXP IRQ pulls. ext0 wake on GPIO 35 LOW therefore picks up both the
  // power button AND a wake-on-motion event with no extra wiring.
  //
  // 20 LSB at ±16G ≈ 156 mg threshold: too high to fire from desk vibrations
  // or HVAC bumps, low enough that picking the device up always trips it.
  M5.Imu.enableWakeOnMotion(M5.Imu.AFS_16G, 20);
  delay(20);                  // let the IMU settle in cycle mode before we sleep
}

void deepSleepFor(uint32_t seconds) {
  clearPowerButtonIrq();      // don't re-wake from the press that put us here
  shutdownPeripherals();
  armImuWakeOnMotion();
  armWakeSources();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void deepSleepUntilButton() {
  clearPowerButtonIrq();
  shutdownPeripherals();
  armWakeSources();
  esp_deep_sleep_start();
}
