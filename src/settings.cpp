// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include "settings.h"
#include <Preferences.h>

static WaterSettings _s;
static Preferences _prefs;

static const uint16_t INTERVAL_CHOICES[] = { 5, 10, 15, 30, 60, 90, 120, 180, 240 };
static const uint8_t  INTERVAL_N = sizeof(INTERVAL_CHOICES) / sizeof(INTERVAL_CHOICES[0]);

static const uint8_t PULSE_LEN_CHOICES[] = { 1, 2, 3, 5, 10 };
static const uint8_t PULSE_LEN_N = sizeof(PULSE_LEN_CHOICES) / sizeof(PULSE_LEN_CHOICES[0]);

static void applyDefaults() {
  _s.intervalMin  = 60;
  _s.anchorHour   = 9;       // first reminder of the day at 09:00 by default
  _s.anchorMinute = 0;
  _s.quietStartH  = 0;       // quiet from midnight
  _s.quietEndH    = 9;       // until 09:00
  _s.brightness   = 2;       // 2/4 (ScreenBreath 60) — easy on the eyes by default
  _s.sound        = true;
  _s.led          = true;
  _s.autoSleep    = true;
  _s.wakeOnMotion = true;    // pick-up wakes the device from deep sleep
  _s.pulseRhythm  = 1;   // "triple" — meatier than a single chirp by default
  _s.pulseLenSec  = 2;   // 2 seconds per pulse (vs the old 220 ms)
  _s.pulseVolume  = 8;   // matches the previously-hardcoded volume
  _s.lastDrankMin = 0;
  _s.nextReminderMin = 0;
  _s.glassesToday = 0;
  _s.lastDay      = 0;
}

void settingsInit() {
  applyDefaults();
  _prefs.begin("water", true);
  _s.intervalMin  = _prefs.getUShort("intv",  _s.intervalMin);
  _s.anchorHour   = _prefs.getUChar ("anh",   _s.anchorHour);
  _s.anchorMinute = _prefs.getUChar ("anm",   _s.anchorMinute);
  _s.quietStartH  = _prefs.getUChar ("qs",    _s.quietStartH);
  _s.quietEndH    = _prefs.getUChar ("qe",    _s.quietEndH);
  _s.brightness   = _prefs.getUChar ("br",    _s.brightness);
  _s.sound        = _prefs.getBool  ("snd",   _s.sound);
  _s.led          = _prefs.getBool  ("led",   _s.led);
  _s.autoSleep    = _prefs.getBool  ("asl",   _s.autoSleep);
  _s.wakeOnMotion = _prefs.getBool  ("wom",   _s.wakeOnMotion);
  _s.pulseRhythm  = _prefs.getUChar ("pr",    _s.pulseRhythm);
  _s.pulseLenSec  = _prefs.getUChar ("plen",  _s.pulseLenSec);
  _s.pulseVolume  = _prefs.getUChar ("vol",   _s.pulseVolume);
  _s.lastDrankMin    = _prefs.getULong ("ldnk",  _s.lastDrankMin);
  _s.nextReminderMin = _prefs.getULong ("next",  _s.nextReminderMin);
  _s.glassesToday    = _prefs.getUShort("gtod",  _s.glassesToday);
  _s.lastDay         = _prefs.getUChar ("lday",  _s.lastDay);
  _prefs.end();
}

WaterSettings& settings() { return _s; }

void settingsSave() {
  _prefs.begin("water", false);
  _prefs.putUShort("intv",  _s.intervalMin);
  _prefs.putUChar ("anh",   _s.anchorHour);
  _prefs.putUChar ("anm",   _s.anchorMinute);
  _prefs.putUChar ("qs",    _s.quietStartH);
  _prefs.putUChar ("qe",    _s.quietEndH);
  _prefs.putUChar ("br",    _s.brightness);
  _prefs.putBool  ("snd",   _s.sound);
  _prefs.putBool  ("led",   _s.led);
  _prefs.putBool  ("asl",   _s.autoSleep);
  _prefs.putBool  ("wom",   _s.wakeOnMotion);
  _prefs.putUChar ("pr",    _s.pulseRhythm);
  _prefs.putUChar ("plen",  _s.pulseLenSec);
  _prefs.putUChar ("vol",   _s.pulseVolume);
  _prefs.putULong ("ldnk",  _s.lastDrankMin);
  _prefs.putULong ("next",  _s.nextReminderMin);
  _prefs.putUShort("gtod",  _s.glassesToday);
  _prefs.putUChar ("lday",  _s.lastDay);
  _prefs.end();
}

bool inQuietHours(uint8_t hour) {
  uint8_t s = _s.quietStartH, e = _s.quietEndH;
  if (s == e) return false;
  if (s < e)  return hour >= s && hour < e;
  return hour >= s || hour < e;
}

const uint16_t* intervalChoices(uint8_t* outN) {
  if (outN) *outN = INTERVAL_N;
  return INTERVAL_CHOICES;
}

uint8_t intervalIndex() {
  for (uint8_t i = 0; i < INTERVAL_N; i++) {
    if (INTERVAL_CHOICES[i] == _s.intervalMin) return i;
  }
  return 4;   // fallback to the 60min slot in {5,10,15,30,60,...}
}

void cycleInterval() {
  uint8_t i = (intervalIndex() + 1) % INTERVAL_N;
  _s.intervalMin = INTERVAL_CHOICES[i];
}

const uint8_t* pulseLenChoices(uint8_t* outN) {
  if (outN) *outN = PULSE_LEN_N;
  return PULSE_LEN_CHOICES;
}

uint8_t pulseLenIndex() {
  for (uint8_t i = 0; i < PULSE_LEN_N; i++) {
    if (PULSE_LEN_CHOICES[i] == _s.pulseLenSec) return i;
  }
  return 1;   // fallback = 2s slot
}

void cyclePulseLen() {
  uint8_t i = (pulseLenIndex() + 1) % PULSE_LEN_N;
  _s.pulseLenSec = PULSE_LEN_CHOICES[i];
}
