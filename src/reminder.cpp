// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include "reminder.h"
#include "ui.h"
#include "settings.h"
#include "power.h"
#include <M5StickCPlus.h>

static const uint32_t REMINDER_TOTAL_MS = 3UL * 60UL * 1000UL;   // 3 minutes
static const uint32_t PULSE_PERIOD_MS   = 30UL * 1000UL;          // start a rhythm every 30 s
static const uint32_t LED_BLINK_MS      = 500;                    // 1 Hz LED blink (idle between pulses)
static const uint32_t LED_FAST_MS       = 100;                    // 5 Hz LED blink during a pulse
static const int LED_PIN = 10;                                    // active-low

// ─── rhythm patterns ─────────────────────────────────────────────────────
// Each step is (frequency Hz, duration ms). freq=0 means silence (gap).
// The engine loops the pattern until the pulse-length budget is exhausted.
struct ToneStep { uint16_t freq; uint16_t durMs; };

static const ToneStep PAT_SINGLE[] = {
  { 1500, 200 }, { 0, 800 }
};
static const ToneStep PAT_TRIPLE[] = {
  { 1500, 120 }, { 0, 80 },
  { 1500, 120 }, { 0, 80 },
  { 1500, 120 }, { 0, 480 }
};
static const ToneStep PAT_LONG[] = {
  { 1200, 800 }, { 0, 200 }
};
static const ToneStep PAT_SOS[] = {
  { 1500, 120 }, { 0, 80 }, { 1500, 120 }, { 0, 80 }, { 1500, 120 }, { 0, 240 },
  { 1500, 320 }, { 0, 80 }, { 1500, 320 }, { 0, 80 }, { 1500, 320 }, { 0, 240 },
  { 1500, 120 }, { 0, 80 }, { 1500, 120 }, { 0, 80 }, { 1500, 120 }, { 0, 480 }
};
static const ToneStep PAT_MELODY[] = {
  {  800, 180 }, { 0, 40 },
  { 1200, 180 }, { 0, 40 },
  { 1800, 220 }, { 0, 400 }
};

static const ToneStep* PATTERN[] = { PAT_SINGLE, PAT_TRIPLE, PAT_LONG, PAT_SOS, PAT_MELODY };
static const uint8_t PATTERN_N[] = {
  sizeof(PAT_SINGLE)/sizeof(ToneStep),
  sizeof(PAT_TRIPLE)/sizeof(ToneStep),
  sizeof(PAT_LONG)  /sizeof(ToneStep),
  sizeof(PAT_SOS)   /sizeof(ToneStep),
  sizeof(PAT_MELODY)/sizeof(ToneStep),
};
static const char* PATTERN_NAME[] = { "single", "triple", "long", "sos", "melody" };

const char* rhythmName(uint8_t idx) {
  return (idx < RH_COUNT) ? PATTERN_NAME[idx] : "?";
}

// ─── non-blocking rhythm playback state machine ──────────────────────────
// `pulseEndAt`  — wall time when the rhythm should stop (overall budget).
// `stepEndAt`   — wall time when the current step finishes; advance then.
// `stepIdx`     — index into the pattern array; wraps via modulo.
// `playing`     — true while the pattern is actively producing audio/gaps.
static uint32_t pulseEndAt   = 0;
static uint32_t stepEndAt    = 0;
static uint8_t  stepIdx      = 0;
static bool     playing      = false;

static void applyVolume() {
  // M5.Beep takes 0..11. Our setting is 0..10 with 0 = silent. Pass through.
  M5.Beep.setVolume(settings().pulseVolume);
}

static void startPulse(uint32_t now) {
  pulseEndAt = now + (uint32_t)settings().pulseLenSec * 1000UL;
  stepEndAt  = now;       // step ends immediately → first step fires next tick
  stepIdx    = 0;
  playing    = true;
  applyVolume();
}

static void tickPulse(uint32_t now) {
  if (!playing) return;
  if ((int32_t)(now - pulseEndAt) >= 0) {
    M5.Beep.mute();
    playing = false;
    return;
  }
  if ((int32_t)(now - stepEndAt) < 0) return;

  uint8_t r = settings().pulseRhythm;
  if (r >= RH_COUNT) r = 0;
  const ToneStep* pat = PATTERN[r];
  uint8_t          n   = PATTERN_N[r];
  const ToneStep& s = pat[stepIdx];

  if (s.freq && settings().sound && settings().pulseVolume > 0) {
    M5.Beep.tone(s.freq, s.durMs);
  } else {
    M5.Beep.mute();
  }
  stepEndAt = now + s.durMs;
  stepIdx   = (stepIdx + 1) % n;
}

// "Tara raraaat" ACK fanfare — small motivating flourish when the user
// confirms they drank. Quick ascending arpeggio (C5, G5, C5, G5, C6-held).
static const ToneStep ACK_FANFARE[] = {
  {  523,  90 },   // C5  "ta"
  {  784,  90 },   // G5  "ra"
  {    0,  40 },   // (gap)
  {  523,  70 },   // C5  "ra"
  {  784,  70 },   // G5  "ra"
  { 1047, 380 },   // C6  "raaat" — held
};

// "Bo-boop" skip blip — a short descending two-note cue when the user passes
// on the drink. Deliberately understated next to the ACK fanfare so the
// reward stays reserved for actually drinking.
static const ToneStep PASS_BLIP[] = {
  {  784, 90 },    // G5
  {  523, 130 },   // C5  (down)
};

static void playSequence(const ToneStep* seq, uint8_t n) {
  applyVolume();
  for (uint8_t i = 0; i < n; i++) {
    const ToneStep& s = seq[i];
    if (s.freq && settings().sound && settings().pulseVolume > 0) M5.Beep.tone(s.freq, s.durMs);
    else                                                          M5.Beep.mute();
    uint32_t until = millis() + s.durMs;
    while ((int32_t)(millis() - until) < 0) {
      M5.Beep.update();
      delay(2);
    }
  }
  M5.Beep.mute();
}

void rhythmPreview(uint8_t idx) {
  if (idx >= RH_COUNT) return;
  // Play one full cycle of the chosen pattern. Blocking is fine here — we're
  // in the menu and patterns are ~1–3 s tops.
  playSequence(PATTERN[idx], PATTERN_N[idx]);
}

void pulsePreview() {
  uint8_t r = settings().pulseRhythm;
  if (r >= RH_COUNT) r = 0;
  const ToneStep* pat = PATTERN[r];
  uint8_t          n   = PATTERN_N[r];
  uint32_t until = millis() + (uint32_t)settings().pulseLenSec * 1000UL;
  applyVolume();

  uint8_t i = 0;
  bool aborted = false;
  while (!aborted && (int32_t)(millis() - until) < 0) {
    const ToneStep& s = pat[i % n];
    if (s.freq && settings().sound && settings().pulseVolume > 0) M5.Beep.tone(s.freq, s.durMs);
    else                                                          M5.Beep.mute();
    uint32_t stepUntil = millis() + s.durMs;
    while ((int32_t)(millis() - stepUntil) < 0 && (int32_t)(millis() - until) < 0) {
      M5.update();           // refresh button state so wasPressed fires
      M5.Beep.update();
      if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) { aborted = true; break; }
      delay(2);
    }
    i++;
  }
  M5.Beep.mute();
}

// ─── LED ─────────────────────────────────────────────────────────────────
static inline void ledOn(bool on) {
  if (settings().led) digitalWrite(LED_PIN, on ? LOW : HIGH);
  else                digitalWrite(LED_PIN, HIGH);
}

// ─── main reminder loop ──────────────────────────────────────────────────
ReminderResult runReminder() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Bring backlight back on after deep sleep (LDO2 powers the LCD backlight
  // rail; ScreenBreath sets PWM level).
  M5.Axp.SetLDO2(true);
  uiSetBrightness(settings().brightness);
  applyVolume();

  uint32_t t0 = millis();
  uint32_t nextPulseAt = t0;            // first pulse fires immediately
  uint32_t lastLedToggleAt = t0;
  bool ledState = false;

  while (true) {
    M5.update();
    M5.Beep.update();
    uint32_t now = millis();
    uint32_t elapsed = now - t0;

    if (M5.BtnA.wasPressed()) {
      M5.Beep.mute();
      playing = false;
      ledOn(false);
      // Motivating "tara raraaat" fanfare — ascending arpeggio + held high
      // note. Blocking ~700 ms is fine; we're exiting the reminder anyway.
      playSequence(ACK_FANFARE, sizeof(ACK_FANFARE) / sizeof(ToneStep));
      return REM_ACK;
    }
    if (M5.BtnB.wasPressed()) {
      // Deliberate skip — user acknowledged the alert but isn't drinking now.
      // No glass counted; the schedule still advances so we nag again later.
      M5.Beep.mute();
      playing = false;
      ledOn(false);
      playSequence(PASS_BLIP, sizeof(PASS_BLIP) / sizeof(ToneStep));
      return REM_PASSED;
    }
    if (powerButtonPressed() == 0x02) {
      M5.Beep.mute();
      playing = false;
      applyVolume();
      M5.Beep.tone(600, 60);
      ledOn(false);
      M5.Beep.update();
      delay(80);
      M5.Beep.update();
      return REM_DISMISSED;
    }
    if (elapsed >= REMINDER_TOTAL_MS) {
      M5.Beep.mute();
      playing = false;
      ledOn(false);
      return REM_TIMEOUT;
    }

    // Trigger the next pulse on schedule.
    if (!playing && (int32_t)(now - nextPulseAt) >= 0) {
      startPulse(now);
      nextPulseAt = now + PULSE_PERIOD_MS;
    }
    tickPulse(now);

    // LED: faster blink while a pulse is playing, slow otherwise.
    uint32_t ledPeriod = playing ? LED_FAST_MS : LED_BLINK_MS;
    if (now - lastLedToggleAt >= ledPeriod) {
      lastLedToggleAt = now;
      ledState = !ledState;
      ledOn(ledState);
    }

    uiDrawReminder(elapsed, REMINDER_TOTAL_MS, playing);
    uiPush();
    delay(20);
  }
}
