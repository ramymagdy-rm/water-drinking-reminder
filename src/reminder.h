// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

enum ReminderResult {
  REM_ACK,        // user pressed A — counted as a glass drunk
  REM_TIMEOUT,    // 3-minute timer elapsed, no acknowledgement
  REM_DISMISSED   // user hit the power button to silence the alert
};

// Blocks until the user presses A (ack) or 3 min elapses (timeout).
// Handles buzz/LED pulses, frame rendering, and button polling internally.
ReminderResult runReminder();

// Rhythm catalog. Used by the settings menu to label the choice and by
// runReminder() to drive playback.
enum Rhythm {
  RH_SINGLE = 0,   // one short chirp
  RH_TRIPLE,       // three short chirps
  RH_LONG,         // one extended tone
  RH_SOS,          // Morse ... --- ...
  RH_MELODY,       // 3 ascending notes (chime)
  RH_COUNT
};

const char* rhythmName(uint8_t idx);

// Blocking preview of a rhythm (used by the menu when the user changes the
// selection so they hear what they're picking). Polls M5.Beep.update().
void rhythmPreview(uint8_t idx);

// Blocking preview of one full pulse — the configured rhythm looped for the
// configured pulseLenSec budget at the configured volume. Mirrors what a
// real reminder will sound like at each 30-second pulse trigger. Any A or B
// press during playback aborts it early.
void pulsePreview();
