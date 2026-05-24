// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

struct WaterSettings {
  uint16_t intervalMin;     // 5..240, default 60
  // Schedule anchor: reminders fire at anchorHour:anchorMinute, then every
  // intervalMin after that, aligned to the wall clock — NOT offset by boot
  // time. e.g. anchor=09:00 + interval=60 → fires at 09:00, 10:00, 11:00, …
  uint8_t  anchorHour;      // 0..23
  uint8_t  anchorMinute;    // 0..59, advanced in steps of 5 via the menu
  uint8_t  quietStartH;     // 0..23, default 22
  uint8_t  quietEndH;       // 0..23, default 7. start==end disables quiet hours.
  uint8_t  brightness;      // 0..4 → ScreenBreath 20..100
  bool     sound;
  bool     led;
  bool     autoSleep;       // false = stay awake in interactive mode until manually slept (power btn)
  bool     wakeOnMotion;    // true = arm MPU6886 wake-on-motion before deep sleep (picking up the device wakes it)
  uint8_t  pulseRhythm;     // 0..RHYTHM_COUNT-1 → see rhythmName() in reminder.cpp
  uint8_t  pulseLenSec;     // seconds the rhythm plays per 30-sec pulse trigger. Allowed: 1,2,3,5,10
  uint8_t  pulseVolume;     // 0..10 buzzer volume (0 = mute, 10 = max). M5.Beep takes 0..11 internally.
  uint32_t lastDrankMin;    // wall-clock minutes since RTC epoch when A was last pressed
  uint32_t nextReminderMin; // scheduled wall-minute of next reminder. 0 = unscheduled (fall back to interval).
  uint16_t glassesToday;
  uint8_t  lastDay;         // RTC day-of-month last seen, used to roll over glassesToday
};

void settingsInit();
WaterSettings& settings();
void settingsSave();

bool inQuietHours(uint8_t hour);
const uint16_t* intervalChoices(uint8_t* outN);   // returns ptr + count
uint8_t intervalIndex();                          // index into intervalChoices() for current setting
void cycleInterval();

const uint8_t* pulseLenChoices(uint8_t* outN);    // {1,2,3,5,10}
uint8_t pulseLenIndex();
void cyclePulseLen();
