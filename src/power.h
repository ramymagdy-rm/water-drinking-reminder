// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

enum WakeReason {
  WAKE_POWER_ON,   // first boot or hard reset
  WAKE_TIMER,      // scheduled reminder
  WAKE_BUTTON,     // user pressed A or B
  WAKE_OTHER
};

WakeReason wakeReason();

// One-time AXP IRQ setup (enable PEK short-press IRQ on the AXP192). Safe to
// call every boot; idempotent. Must be called after M5.begin() so the AXP
// I2C bus is up.
void powerInit();

// Read & clear the AXP PEK button-press status. Returns 0x02 on short-press,
// 0x01 on long-press, 0 otherwise. Use this in the awake loop to detect a
// power-button tap.
uint8_t powerButtonPressed();

// True once the RTC has been set to a sane year (>= 2024). False on first
// boot — quiet hours are skipped while this is false to avoid a fresh
// device silently sleeping through everything because RTC reads 2000-01-01.
bool rtcIsValid();

// Wall-clock minute counter from the RTC (year/month/day ignored — we only
// care about elapsed minutes for "X min ago" displays). Wraps every ~32 years
// which is fine.
uint32_t rtcWallMinutes();

// Seconds until the next reminder should fire. Honors interval + quiet hours
// + any persisted schedule from scheduleNextReminder().
uint32_t secondsUntilNextReminder();

// Minutes remaining until the next scheduled reminder, for the home-page
// countdown. Returns 0 if the schedule has passed, or settings().intervalMin
// if no schedule is set (RTC unset / fresh install).
uint32_t minutesUntilNextReminder();

// Persist the next reminder's wall-minute = rtcWallMinutes() + intervalMin.
// Call after a reminder fires, on cold boot, or when the user changes the
// interval / RTC. No-op if RTC is invalid (year < 2024).
void scheduleNextReminder();

// Deep sleep with timer + button wakeup armed. Does not return.
void deepSleepFor(uint32_t seconds);

// Deep sleep with only button wakeup armed (no timer).
void deepSleepUntilButton();
