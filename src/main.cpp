// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include <M5StickCPlus.h>
#include "settings.h"
#include "power.h"
#include "ui.h"
#include "reminder.h"

static void applyVolume() { M5.Beep.setVolume(settings().pulseVolume); }

// Shutdown confirmation window. The user has to tap B twice on the
// `shutdown` menu item within 3 s to actually power the device off — first
// tap arms (and uiFormatMenuValue swaps the right-side label to "really?"),
// second tap executes M5.Axp.PowerOff().
static uint32_t shutdownArmedUntil = 0;
bool menuShutdownArmed() {
  return shutdownArmedUntil != 0 && (int32_t)(millis() - shutdownArmedUntil) < 0;
}
static void disarmShutdown() { shutdownArmedUntil = 0; }

static const uint32_t IDLE_SLEEP_MS = 30UL * 1000UL;
static const int LED_PIN = 10;

// Interactive-mode state. Only used after a button or power-on wake.
static Page    currentPage    = PAGE_HOME;
static bool    menuOpen       = false;
static uint8_t menuPage       = MP_REMINDER;
static uint8_t menuSel        = 0;
static uint32_t lastInteract  = 0;
static bool    btnALongFired  = false;

// Face-down nap state (dim + pause UI render, screen stays powered).
// Hysteresis counter mirrors the buddy: strict to enter, loose to exit, so
// IMU noise on the threshold doesn't flap brightness every other frame.
static bool    napping        = false;
static int8_t  faceDownFrames = 0;

static bool isFaceDown() {
  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);
  return az < -0.7f && fabsf(ax) < 0.4f && fabsf(ay) < 0.4f;
}

static void rolloverDailyGlasses() {
  if (!rtcIsValid()) return;
  RTC_DateTypeDef dt; M5.Rtc.GetDate(&dt);
  if (settings().lastDay == 0) {
    settings().lastDay = dt.Date;
    settingsSave();
    return;
  }
  if (settings().lastDay != dt.Date) {
    settings().glassesToday = 0;
    settings().lastDay = dt.Date;
    settingsSave();
  }
}

static void onAckDrank() {
  rolloverDailyGlasses();
  settings().glassesToday++;
  if (rtcIsValid()) settings().lastDrankMin = rtcWallMinutes();
  settingsSave();
}

static void menuActivate() {
  WaterSettings& s = settings();
  RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
  uint8_t mi = menuItemAt(menuPage, menuSel);
  switch (mi) {
    case MI_INTERVAL:
      cycleInterval();
      scheduleNextReminder();      // new interval → reschedule from now
      break;
    case MI_ANCHOR_HOUR:
      s.anchorHour = (s.anchorHour + 1) % 24;
      scheduleNextReminder();      // new anchor → align schedule to it
      break;
    case MI_ANCHOR_MIN:
      s.anchorMinute = (s.anchorMinute + 5) % 60;   // 5-minute granularity
      scheduleNextReminder();
      break;
    case MI_QUIET_START: s.quietStartH = (s.quietStartH + 1) % 24; break;
    case MI_QUIET_END:   s.quietEndH   = (s.quietEndH   + 1) % 24; break;
    case MI_BRIGHTNESS:
      s.brightness = (s.brightness + 1) % 5;
      uiSetBrightness(s.brightness);
      break;
    case MI_SOUND:      s.sound     = !s.sound;     break;
    case MI_VOLUME:
      s.pulseVolume = (s.pulseVolume + 1) % 11;     // 0..10
      applyVolume();
      // Brief audible preview so the user can dial in the level.
      if (s.sound && s.pulseVolume > 0) {
        M5.Beep.tone(1500, 120);
        uint32_t until = millis() + 140;
        while ((int32_t)(millis() - until) < 0) { M5.Beep.update(); delay(2); }
        M5.Beep.mute();
      }
      break;
    case MI_PULSE_RHYTHM:
      s.pulseRhythm = (s.pulseRhythm + 1) % RH_COUNT;
      // Play a full cycle of the new pattern so the user hears the rhythm.
      rhythmPreview(s.pulseRhythm);
      break;
    case MI_PULSE_LEN:    cyclePulseLen(); break;
    case MI_PULSE_PREVIEW: pulsePreview(); break;
    case MI_LED:        s.led       = !s.led;       break;
    case MI_AUTO_SLEEP:   s.autoSleep    = !s.autoSleep;    break;
    case MI_WAKE_MOTION:  s.wakeOnMotion = !s.wakeOnMotion; break;
    case MI_SHUTDOWN:
      // Tap-twice confirm. The first B arms ("really?" appears as the value);
      // a second B within 3 s actually powers off. Any A-press disarms.
      if (menuShutdownArmed()) {
        settingsSave();
        if (s.sound && s.pulseVolume > 0) {
          applyVolume();
          M5.Beep.tone(400, 350);
          uint32_t until = millis() + 380;
          while ((int32_t)(millis() - until) < 0) { M5.Beep.update(); delay(2); }
          M5.Beep.mute();
        }
        // Cut all rails via AXP shutdown register. Device turns back on when
        // the user presses the power button (cold-boot path, WAKE_POWER_ON).
        M5.Axp.PowerOff();
        // Never returns; AXP yanks power.
        while (true) { delay(1000); }
      } else {
        shutdownArmedUntil = millis() + 3000;
        if (s.sound && s.pulseVolume > 0) M5.Beep.tone(1400, 80);
      }
      return;           // skip the settingsSave() at the bottom of menuActivate
    case MI_SET_HOUR:
      tm.Hours = (tm.Hours + 1) % 24;
      M5.Rtc.SetTime(&tm);
      // First time the user sets the clock, force the date out of year 2000
      // so quiet hours arm. Calendar logic doesn't need an accurate date —
      // just a year ≥ 2024 to satisfy rtcIsValid().
      {
        RTC_DateTypeDef dt; M5.Rtc.GetDate(&dt);
        if (dt.Year < 2024) {
          dt.Year = 2024; dt.Month = 1; dt.Date = 1; dt.WeekDay = 1;
          M5.Rtc.SetDate(&dt);
        }
      }
      scheduleNextReminder();      // RTC moved → recompute schedule from new now
      break;
    case MI_SET_MINUTE:
      tm.Minutes = (tm.Minutes + 1) % 60;
      tm.Seconds = 0;
      M5.Rtc.SetTime(&tm);
      scheduleNextReminder();
      break;
    case MI_NEXT_PAGE:
      menuPage = (menuPage + 1) % MP_COUNT;
      menuSel  = 0;
      disarmShutdown();
      return;                       // skip settingsSave at the bottom
    case MI_EXIT:
      menuOpen = false;
      settingsSave();
      break;
  }
  settingsSave();
}

static void enterInteractiveMode() {
  M5.Axp.SetLDO2(true);
  uiSetBrightness(settings().brightness);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  currentPage    = PAGE_HOME;
  menuOpen       = false;
  menuPage       = MP_REMINDER;
  menuSel        = 0;
  napping        = false;
  faceDownFrames = 0;
  lastInteract   = millis();
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(0);
  M5.Imu.Init();
  M5.Beep.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  settingsInit();
  uiInit();
  uiSetBrightness(settings().brightness);
  applyVolume();              // apply persisted buzzer volume
  powerInit();                // enable AXP PEK short-press IRQ for wake

  WakeReason wr = wakeReason();

  if (wr == WAKE_TIMER) {
    // Scheduled reminder. Honor quiet hours as a safety check — the scheduler
    // tries to avoid landing in quiet, but if quiet hours were edited after
    // the last sleep we might wake mid-quiet.
    if (rtcIsValid()) {
      RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
      if (inQuietHours(tm.Hours)) {
        deepSleepFor(secondsUntilNextReminder());
        return;
      }
    }
    rolloverDailyGlasses();
    ReminderResult r = runReminder();
    if (r == REM_ACK) {
      onAckDrank();
      uiDrawAck();
      delay(1500);
    } else if (r == REM_TIMEOUT) {
      uiDrawTimeout();
      delay(800);
    }
    // REM_DISMISSED: silent — power button means "shut up and let me work".
    scheduleNextReminder();           // advance schedule = now + interval
    deepSleepFor(secondsUntilNextReminder());
    return;
  }

  // POWER_ON or BUTTON → interactive mode.
  rolloverDailyGlasses();
  enterInteractiveMode();
  if (wr == WAKE_POWER_ON) {
    // Cold boot — reset schedule so the countdown starts fresh from now.
    // (Soft wake from a button keeps any existing schedule running.)
    scheduleNextReminder();
    uiDrawSplash();
    delay(1200);
  }
}

void loop() {
  M5.update();
  M5.Beep.update();
  uint32_t now = millis();

  // ─── face-down nap: dim screen and pause UI when face-down, undim on flip
  // up. Hysteresis: need 15 consecutive face-down frames to enter the nap,
  // 8 not-face-down frames to leave. Keeps the screen from flickering when
  // the device is held near the threshold.
  bool down = isFaceDown();
  if (down) { if (faceDownFrames < 20) faceDownFrames++; }
  else      { if (faceDownFrames > -10) faceDownFrames--; }

  if (!napping && faceDownFrames >= 15) {
    napping = true;
    M5.Axp.ScreenBreath(8);     // very dim — leaves backlight on so flip-up
                                // detection is instant on the next frame
  } else if (napping && faceDownFrames <= -8) {
    napping = false;
    uiSetBrightness(settings().brightness);
    lastInteract = now;         // count the flip-up as activity
  }

  // ─── power button: deep sleep on short press. Long-press (4 s+) is
  // handled by AXP hardware and cuts power entirely.
  if (powerButtonPressed() == 0x02) {
    if (settings().sound) M5.Beep.tone(600, 60);
    M5.Beep.update();
    delay(80);
    if (menuOpen) settingsSave();
    deepSleepFor(secondsUntilNextReminder());
    return;
  }

  // ─── due reminder while awake: when the scheduled minute arrives during
  // interactive mode (either because auto-sleep is off, or because the user
  // is poking buttons right at the scheduled time), fire the alert in-place
  // instead of waiting for the next deep-sleep cycle. Quiet hours block it.
  if (rtcIsValid() && settings().nextReminderMin > 0) {
    RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
    if (!inQuietHours(tm.Hours) && rtcWallMinutes() >= settings().nextReminderMin) {
      rolloverDailyGlasses();
      ReminderResult r = runReminder();
      if (r == REM_ACK) {
        onAckDrank();
        uiDrawAck(); uiPush();
        delay(1500);
      } else if (r == REM_TIMEOUT) {
        uiDrawTimeout(); uiPush();
        delay(800);
      }
      scheduleNextReminder();
      lastInteract = millis();   // don't immediately auto-sleep after the alert
      return;                    // restart the loop so the next iteration draws cleanly
    }
  }

  // ─── A / B input handling (skip while napping so a face-down handle bump
  // can't accidentally cycle pages or open the menu)
  if (!napping) {
    if (M5.BtnA.isPressed() || M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      lastInteract = now;
    }

    if (M5.BtnA.pressedFor(600) && !btnALongFired) {
      btnALongFired = true;
      if (settings().sound) M5.Beep.tone(800, 60);
      menuOpen = !menuOpen;
      if (menuOpen) { menuPage = MP_REMINDER; menuSel = 0; }
      else          { settingsSave(); }
      disarmShutdown();           // closing/opening the menu cancels confirm
      lastInteract = now;
    }

    if (M5.BtnA.wasReleased()) {
      if (!btnALongFired) {
        if (settings().sound) M5.Beep.tone(1800, 30);
        if (menuOpen) {
          uint8_t plen = menuPageLen(menuPage);
          if (plen > 0) menuSel = (menuSel + 1) % plen;
          disarmShutdown();       // navigating away cancels confirm
        } else {
          currentPage = (Page)((currentPage + 1) % PAGE_COUNT);
        }
        lastInteract = now;
      }
      btnALongFired = false;
    }

    if (M5.BtnB.wasPressed()) {
      if (settings().sound) M5.Beep.tone(2400, 30);
      if (menuOpen) menuActivate();
      lastInteract = now;
    }
  }

  // ─── draw (skip while napping — dimmed screen shows whatever was last
  // pushed; saves SPI traffic and lets the dim feel like a real "off")
  if (!napping) {
    if (menuOpen) uiDrawMenu(menuPage, menuSel);
    else          uiDrawPage(currentPage);
    uiPush();
  }

  // ─── idle → deep sleep. Gated on the auto-sleep setting (default on).
  // Skipped while napping — flipping face-down isn't "idle", it's a
  // deliberate suppress, and waking it via flip-up is the user's choice.
  if (settings().autoSleep && !napping && now - lastInteract > IDLE_SLEEP_MS) {
    if (menuOpen) settingsSave();
    deepSleepFor(secondsUntilNextReminder());
  }

  delay(30);
}
