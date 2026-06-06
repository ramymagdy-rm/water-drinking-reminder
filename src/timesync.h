// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

// NTP time sync over WiFi. Runs once per wake (every wake from deep sleep is
// a full reboot, so calling timeSyncBegin() in setup() = sync-per-wake).
// Credentials come from src/secrets.h (gitignored); with an empty SSID the
// whole module is a no-op so the firmware still works WiFi-less.
//
// The synced UTC time is converted to local (EET/EEST, automatic DST) and
// written into the BM8563 hardware RTC, which the rest of the firmware
// already treats as the single source of truth.

enum TimeSyncState {
  TS_IDLE,      // not started, or already finished (result consumed)
  TS_RUNNING,   // WiFi associating / waiting for the NTP response
  TS_DONE,      // RTC updated this poll — returned exactly once
  TS_FAILED     // timed out or unconfigured; WiFi shut down
};

// Kick off WiFi + SNTP, non-blocking. No-op if already active or if
// secrets.h has an empty SSID. Safe to call once per boot.
void timeSyncBegin();

// Advance the state machine; call from a loop. Returns TS_DONE exactly once,
// on the poll where the RTC was written — recompute the reminder schedule
// when you see it.
TimeSyncState timeSyncPoll();

// Block until the sync started by timeSyncBegin() concludes (done, failed,
// or the 10 s overall budget expires). Starts the sync itself if it wasn't
// begun. Returns true if the RTC was updated.
bool timeSyncFinish();
