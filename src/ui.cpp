// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include "ui.h"
#include "settings.h"
#include "power.h"
#include "reminder.h"
#include <M5StickCPlus.h>
#include <stdio.h>

static TFT_eSprite spr = TFT_eSprite(&M5.Lcd);

// 135×240 portrait.
static const int W = 135;
static const int H = 240;
static const int CX = W / 2;

// Palette
static const uint16_t BG       = 0x0000;
static const uint16_t INK      = 0xFFFF;
static const uint16_t DIM      = 0x8410;
static const uint16_t WATER    = 0x041F;   // strong blue
static const uint16_t WATER_LT = 0x04FF;   // lighter blue
static const uint16_t CYAN_C     = 0x07FF;
static const uint16_t GREEN_C  = 0x07E0;
static const uint16_t RED_C    = 0xF800;
static const uint16_t ORANGE_C   = 0xFA20;
static const uint16_t PANEL    = 0x2104;

void uiInit() {
  spr.createSprite(W, H);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);
}

void uiSetBrightness(uint8_t level) {
  if (level > 4) level = 4;
  M5.Axp.ScreenBreath(20 + level * 20);
}

void uiPush() { spr.pushSprite(0, 0); }

// ───────── splash ─────────
void uiDrawSplash() {
  spr.fillSprite(BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(WATER_LT, BG);
  spr.drawString("Water", CX, H / 2 - 24);
  spr.drawString("Reminder", CX, H / 2);
  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  spr.drawString("stay hydrated", CX, H / 2 + 28);
  spr.setTextDatum(TL_DATUM);
  uiPush();
}

// ───────── header strip (page name + page index dots) ─────────
static void drawHeader(const char* title, Page p) {
  spr.fillRect(0, 0, W, 18, PANEL);
  spr.drawFastHLine(0, 18, W, DIM);
  spr.setTextSize(1);
  spr.setTextColor(INK, PANEL);
  spr.setCursor(6, 6);
  spr.print(title);

  // page-index dots, right side
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = W - 8 - (PAGE_COUNT - 1 - i) * 8;
    if (i == p) spr.fillCircle(x, 9, 2, INK);
    else        spr.drawCircle(x, 9, 2, DIM);
  }
}

// ───────── glass icon (used on home page and on reminder screen) ─────────
// cx,cy = top-center of glass. fillFrac 0..1 = water level. ww = phase for
// the surface wave so it shimmers a little when redrawn at different times.
static void drawGlass(int cx, int cy, int w, int h, float fillFrac, uint16_t outline, uint16_t water, int waveTick) {
  int x = cx - w / 2;
  // outline: walls and base (not the top — open glass)
  spr.drawLine(x,         cy,      x,         cy + h, outline);
  spr.drawLine(x + w - 1, cy,      x + w - 1, cy + h, outline);
  spr.drawLine(x,         cy + h,  x + w - 1, cy + h, outline);
  // small rim feet
  spr.drawFastVLine(x - 2, cy, 3, outline);
  spr.drawFastVLine(x + w + 1, cy, 3, outline);

  // water
  if (fillFrac < 0) fillFrac = 0;
  if (fillFrac > 1) fillFrac = 1;
  int waterH = (int)((h - 2) * fillFrac);
  if (waterH > 0) {
    int wy = cy + h - 1 - waterH;
    spr.fillRect(x + 1, wy, w - 2, waterH, water);
    // wave on top — alternating 2px crests
    for (int i = 1; i < w - 1; i += 2) {
      int yoff = ((i / 2 + waveTick) % 2) ? -1 : 0;
      spr.drawFastHLine(x + i, wy + yoff, 2, water);
    }
    // tiny highlight
    spr.drawFastVLine(x + 2, wy + 2, waterH - 3, WATER_LT);
  }
}

// ───────── home page ─────────
static void formatRelativeMinutes(uint32_t deltaMin, char* out, uint8_t outSize) {
  if (deltaMin == 0)        snprintf(out, outSize, "just now");
  else if (deltaMin < 60)   snprintf(out, outSize, "%u min ago", (unsigned)deltaMin);
  else if (deltaMin < 1440) snprintf(out, outSize, "%uh %02um ago", (unsigned)(deltaMin/60), (unsigned)(deltaMin%60));
  else                      snprintf(out, outSize, "%u days ago", (unsigned)(deltaMin/1440));
}

static void drawHome() {
  spr.fillSprite(BG);
  drawHeader("HOME", PAGE_HOME);

  // Small tagline
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(1);
  spr.setTextColor(WATER_LT, BG);
  spr.drawString("Stay hydrated", CX, 28);

  // Glass icon — centered, smaller than before so the countdown gets the room
  drawGlass(CX, 42, 32, 54, 0.7f, INK, WATER, (millis() / 500) & 1);

  // Divider above the countdown block
  spr.drawFastHLine(20, 112, W - 40, DIM);

  // Countdown — the headline of the home page
  spr.setTextColor(DIM, BG);
  spr.drawString("next reminder in", CX, 122);

  char buf[24];
  uint32_t mins = minutesUntilNextReminder();
  bool overdue = (mins == 0);

  // Format big number. <60 → "23"; ≥60 → "1:30" (hours:mins).
  char big[8];
  if (overdue)        snprintf(big, sizeof(big), "now");
  else if (mins < 60) snprintf(big, sizeof(big), "%u", (unsigned)mins);
  else                snprintf(big, sizeof(big), "%u:%02u", (unsigned)(mins/60), (unsigned)(mins%60));

  // Size 4 fits "60" / "now" / "9:59"; for "1:30" (4 chars * 24 = 96px wide) too.
  // Drop to size 3 for the very longest cases ("4:00" never appears since max
  // is 240 min, but defensive).
  uint8_t bigSize = (strlen(big) >= 5) ? 3 : 4;
  spr.setTextSize(bigSize);
  spr.setTextColor(overdue ? ORANGE_C : WATER_LT, BG);
  spr.drawString(big, CX, 148);

  // "min" label only when we showed a bare number (not for "1:30" or "now")
  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  if (!overdue && mins < 60) spr.drawString("min", CX, 174);

  // Divider below
  spr.drawFastHLine(20, 188, W - 40, DIM);

  // Last drank — second-class info under the divider
  spr.setTextColor(DIM, BG);
  spr.drawString("last drink", CX, 198);
  if (settings().lastDrankMin == 0) {
    spr.setTextColor(DIM, BG);
    spr.drawString("—", CX, 212);
  } else {
    uint32_t now = rtcWallMinutes();
    uint32_t delta = (now >= settings().lastDrankMin) ? now - settings().lastDrankMin : 0;
    formatRelativeMinutes(delta, buf, sizeof(buf));
    spr.setTextColor(INK, BG);
    spr.drawString(buf, CX, 212);
  }

  spr.setTextDatum(TL_DATUM);
}

// ───────── stats page ─────────
static void drawStats() {
  spr.fillSprite(BG);
  drawHeader("STATS", PAGE_STATS);

  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  spr.setCursor(6, 28);
  spr.print("today");

  // Big glass count
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", (unsigned)settings().glassesToday);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(6);
  spr.setTextColor(WATER_LT, BG);
  spr.drawString(buf, CX, 70);
  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  spr.drawString("glasses", CX, 108);

  // Glass row visual — fill up to 8 glasses, then just show the count
  spr.setTextDatum(TL_DATUM);
  uint16_t shown = settings().glassesToday;
  if (shown > 8) shown = 8;
  int gx = 14;
  for (int i = 0; i < 8; i++) {
    bool full = i < shown;
    drawGlass(gx + i * 14, 130, 10, 18, full ? 1.0f : 0.0f, full ? INK : DIM, WATER, 0);
  }

  // Clock readout
  RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(INK, BG);
  snprintf(buf, sizeof(buf), "%02u:%02u", tm.Hours, tm.Minutes);
  spr.drawString(buf, CX, 178);

  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  if (inQuietHours(tm.Hours)) spr.drawString("(quiet hours)", CX, 200);
  else                        spr.drawString("hold A: menu", CX, 200);
  spr.drawString("A: next page", CX, 214);
  spr.setTextDatum(TL_DATUM);
}

// ───────── battery page ─────────
static void drawBattery() {
  spr.fillSprite(BG);
  drawHeader("BATTERY", PAGE_BATTERY);

  int vBat_mV = (int)(M5.Axp.GetBatVoltage() * 1000);
  int iBat_mA = (int)M5.Axp.GetBatCurrent();
  int vBus_mV = (int)(M5.Axp.GetVBusVoltage() * 1000);
  int pct = (vBat_mV - 3200) / 10;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  bool usb = vBus_mV > 4000;
  bool charging = usb && iBat_mA > 1;
  bool full = usb && vBat_mV > 4100 && iBat_mA < 10;

  // Big percentage
  char buf[24];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(5);
  uint16_t pctCol = pct < 20 ? RED_C : (pct < 50 ? ORANGE_C : GREEN_C);
  spr.setTextColor(pctCol, BG);
  spr.drawString(buf, CX, 70);

  // Status line
  spr.setTextSize(2);
  spr.setTextColor(full ? GREEN_C : (charging ? CYAN_C : (usb ? CYAN_C : DIM)), BG);
  const char* status = full ? "full" : charging ? "charging" : usb ? "usb idle" : "battery";
  spr.drawString(status, CX, 110);

  // Battery bar
  spr.setTextDatum(TL_DATUM);
  int bx = 16, by = 134, bw = W - 32, bh = 14;
  spr.drawRoundRect(bx, by, bw, bh, 2, INK);
  spr.fillRect(bx + bw, by + 3, 3, bh - 6, INK);
  int fillW = (bw - 4) * pct / 100;
  if (fillW > 0) spr.fillRect(bx + 2, by + 2, fillW, bh - 4, pctCol);

  // Details
  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  spr.setCursor(8, 160);  spr.printf("voltage   %d.%02d V", vBat_mV/1000, (vBat_mV%1000)/10);
  spr.setCursor(8, 172);  spr.printf("current   %+d mA", iBat_mA);
  if (usb) {
    spr.setCursor(8, 184); spr.printf("usb in    %d.%02d V", vBus_mV/1000, (vBus_mV%1000)/10);
  }
  spr.setCursor(8, 198);  spr.printf("uptime    %lus", (unsigned long)(millis() / 1000));

  spr.setTextColor(DIM, BG);
  spr.setTextDatum(MC_DATUM);
  spr.drawString("hold A: menu", CX, 220);
  spr.setTextDatum(TL_DATUM);
}

void uiDrawPage(Page p) {
  switch (p) {
    case PAGE_HOME:    drawHome();    break;
    case PAGE_STATS:   drawStats();   break;
    case PAGE_BATTERY: drawBattery(); break;
    default: break;
  }
}

// ───────── menu ─────────
// Per-page item lists. Each page ends with MI_NEXT_PAGE then MI_EXIT so the
// user always has a way to flip pages or close the menu without scrolling
// far.
static const uint8_t REMINDER_ITEMS[] = {
  MI_INTERVAL,
  MI_ANCHOR_HOUR, MI_ANCHOR_MIN,
  MI_QUIET_START, MI_QUIET_END,
  MI_PULSE_RHYTHM, MI_PULSE_LEN, MI_VOLUME, MI_PULSE_PREVIEW,
  MI_NEXT_PAGE, MI_EXIT,
};
static const uint8_t DEVICE_ITEMS[] = {
  MI_BRIGHTNESS, MI_SOUND, MI_LED, MI_AUTO_SLEEP, MI_WAKE_MOTION,
  MI_SET_HOUR, MI_SET_MINUTE,
  MI_SHUTDOWN,
  MI_NEXT_PAGE, MI_EXIT,
};

uint8_t menuItemAt(uint8_t page, uint8_t sel) {
  if (page == MP_REMINDER && sel < sizeof(REMINDER_ITEMS)) return REMINDER_ITEMS[sel];
  if (page == MP_DEVICE   && sel < sizeof(DEVICE_ITEMS))   return DEVICE_ITEMS[sel];
  return 0xFF;
}
uint8_t menuPageLen(uint8_t page) {
  if (page == MP_REMINDER) return sizeof(REMINDER_ITEMS);
  if (page == MP_DEVICE)   return sizeof(DEVICE_ITEMS);
  return 0;
}
const char* menuPageName(uint8_t page) {
  return page == MP_REMINDER ? "REMINDER" : page == MP_DEVICE ? "DEVICE" : "";
}

const char* uiMenuItemLabel(uint8_t i) {
  switch (i) {
    case MI_INTERVAL:    return "interval";
    case MI_ANCHOR_HOUR: return "start hr";
    case MI_ANCHOR_MIN:  return "start min";
    case MI_QUIET_START: return "quiet from";
    case MI_QUIET_END:   return "quiet to";
    case MI_BRIGHTNESS:  return "bright";
    case MI_SOUND:       return "sound";
    case MI_VOLUME:      return "volume";
    case MI_PULSE_RHYTHM:return "rhythm";
    case MI_PULSE_LEN:   return "buzz len";
    case MI_PULSE_PREVIEW:return "preview";
    case MI_LED:         return "led";
    case MI_AUTO_SLEEP:  return "auto-sleep";
    case MI_WAKE_MOTION: return "wake on lift";
    case MI_SET_HOUR:    return "set hour";
    case MI_SET_MINUTE:  return "set min";
    case MI_SHUTDOWN:    return "shutdown";
    case MI_NEXT_PAGE:   return "next page";
    case MI_EXIT:        return "exit";
    default:             return "";
  }
}

void uiFormatMenuValue(uint8_t i, char* out, uint8_t outSize) {
  WaterSettings& s = settings();
  RTC_TimeTypeDef tm; M5.Rtc.GetTime(&tm);
  switch (i) {
    case MI_INTERVAL:
      if (s.intervalMin >= 60 && s.intervalMin % 60 == 0)
        snprintf(out, outSize, "%uh", (unsigned)(s.intervalMin / 60));
      else if (s.intervalMin >= 60)
        snprintf(out, outSize, "%uh%02u", (unsigned)(s.intervalMin/60), (unsigned)(s.intervalMin%60));
      else
        snprintf(out, outSize, "%um", (unsigned)s.intervalMin);
      break;
    case MI_ANCHOR_HOUR: snprintf(out, outSize, "%02u",     s.anchorHour);   break;
    case MI_ANCHOR_MIN:  snprintf(out, outSize, "%02u",     s.anchorMinute); break;
    case MI_QUIET_START: snprintf(out, outSize, "%02u:00", s.quietStartH); break;
    case MI_QUIET_END:   snprintf(out, outSize, "%02u:00", s.quietEndH);   break;
    case MI_BRIGHTNESS:  snprintf(out, outSize, "%u/4",    s.brightness);  break;
    case MI_SOUND:       snprintf(out, outSize, "%s", s.sound     ? "on" : "off"); break;
    case MI_VOLUME:      snprintf(out, outSize, "%u/10", (unsigned)s.pulseVolume); break;
    case MI_PULSE_RHYTHM:snprintf(out, outSize, "%s", rhythmName(s.pulseRhythm));   break;
    case MI_PULSE_LEN:   snprintf(out, outSize, "%us", (unsigned)s.pulseLenSec);    break;
    case MI_PULSE_PREVIEW:snprintf(out, outSize, "B:play");                         break;
    case MI_LED:         snprintf(out, outSize, "%s", s.led       ? "on" : "off"); break;
    case MI_AUTO_SLEEP:  snprintf(out, outSize, "%s", s.autoSleep    ? "on" : "off"); break;
    case MI_WAKE_MOTION: snprintf(out, outSize, "%s", s.wakeOnMotion ? "on" : "off"); break;
    case MI_SET_HOUR:    snprintf(out, outSize, "%02u",     tm.Hours);     break;
    case MI_SET_MINUTE:  snprintf(out, outSize, "%02u",     tm.Minutes);   break;
    case MI_SHUTDOWN:    snprintf(out, outSize, "%s", menuShutdownArmed() ? "really?" : "B:off"); break;
    case MI_NEXT_PAGE:   snprintf(out, outSize, "B:nxt");      break;
    case MI_EXIT:        out[0] = 0; break;
    default:             out[0] = 0; break;
  }
}

void uiDrawMenu(uint8_t page, uint8_t selected) {
  if (page >= MP_COUNT) page = 0;
  spr.fillSprite(BG);

  // Header — page name on the left, page indicator dots on the right.
  spr.fillRect(0, 0, W, 18, PANEL);
  spr.drawFastHLine(0, 18, W, DIM);
  spr.setTextSize(1);
  spr.setTextColor(INK, PANEL);
  spr.setCursor(6, 6);
  spr.print(menuPageName(page));
  // Page dots, right of header
  for (uint8_t i = 0; i < MP_COUNT; i++) {
    int x = W - 8 - (MP_COUNT - 1 - i) * 8;
    if (i == page) spr.fillCircle(x, 9, 2, INK);
    else           spr.drawCircle(x, 9, 2, DIM);
  }

  // Item rows. rowH=14 keeps the longest page (REMINDER, 12 items) within
  // 24 + 12*14 = 192 px, leaving ~48 px below the last row.
  int top = 24, rowH = 14;
  char val[16];
  uint8_t n = menuPageLen(page);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t mi = menuItemAt(page, i);
    int y = top + i * rowH;
    bool sel = (i == selected);
    uint16_t fg = sel ? INK : DIM;
    if (sel) {
      spr.fillRect(0, y - 2, W, rowH, PANEL);
      spr.fillTriangle(2, y, 2, y + 8, 6, y + 4, INK);
    }
    spr.setTextColor(fg, sel ? PANEL : BG);
    spr.setCursor(10, y);
    spr.print(uiMenuItemLabel(mi));

    uiFormatMenuValue(mi, val, sizeof(val));
    spr.setTextColor(sel ? CYAN_C : DIM, sel ? PANEL : BG);
    int rx = W - 6 - (int)strlen(val) * 6;
    spr.setCursor(rx, y);
    spr.print(val);
  }
}

// ───────── reminder screen ─────────
void uiDrawReminder(uint32_t elapsedMs, uint32_t totalMs, bool pulseActive) {
  spr.fillSprite(BG);

  // Header
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  uint16_t headCol = pulseActive ? INK : WATER_LT;
  spr.setTextColor(headCol, BG);
  spr.drawString("DRINK", CX, 22);
  spr.drawString("WATER!", CX, 42);

  // Big animated glass — water rises and falls so it doesn't feel static
  float phase = (elapsedMs % 2400) / 2400.0f;     // 0..1 over 2.4s
  float fill  = 0.4f + 0.3f * (phase < 0.5f ? phase * 2 : (1 - phase) * 2);
  uint16_t glassCol = pulseActive ? INK : WATER_LT;
  drawGlass(CX, 70, 56, 90, fill, glassCol, WATER, (elapsedMs / 300) & 3);

  // Falling droplet above the glass (only when not pulsing white, to keep
  // the pulse moment visually distinct).
  if (!pulseActive) {
    uint32_t dropPhase = (elapsedMs % 1200);
    int dy = 24 + (int)(dropPhase * 36 / 1200);    // 24..60
    spr.fillCircle(CX, dy, 3, WATER_LT);
  }

  // Countdown
  uint32_t remainMs = (elapsedMs >= totalMs) ? 0 : totalMs - elapsedMs;
  uint32_t rs = remainMs / 1000;
  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%lu:%02lu", (unsigned long)(rs / 60), (unsigned long)(rs % 60));
  spr.setTextSize(2);
  spr.setTextColor(rs < 30 ? RED_C : DIM, BG);
  spr.drawString(tbuf, CX, 184);

  // Action hint
  spr.setTextSize(1);
  spr.setTextColor(GREEN_C, BG);
  spr.drawString("A: drank it!", CX, 214);
  spr.setTextDatum(TL_DATUM);
}

void uiDrawAck() {
  spr.fillSprite(BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(3);
  spr.setTextColor(GREEN_C, BG);
  spr.drawString("Great!", CX, H / 2 - 30);

  // little checkmark
  int cx = CX, cy = H / 2 + 4;
  spr.fillCircle(cx, cy, 22, GREEN_C);
  spr.drawLine(cx - 10, cy + 2, cx - 2, cy + 10, BG);
  spr.drawLine(cx - 10, cy + 1, cx - 2, cy + 9,  BG);
  spr.drawLine(cx - 9,  cy + 2, cx - 2, cy + 9,  BG);
  spr.drawLine(cx - 2,  cy + 10, cx + 12, cy - 6, BG);
  spr.drawLine(cx - 2,  cy + 9,  cx + 12, cy - 7, BG);
  spr.drawLine(cx - 1,  cy + 10, cx + 12, cy - 5, BG);

  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "%u glasses today", (unsigned)settings().glassesToday);
  spr.drawString(buf, CX, H / 2 + 50);
  spr.setTextDatum(TL_DATUM);
  uiPush();
}

void uiDrawTimeout() {
  spr.fillSprite(BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(ORANGE_C, BG);
  spr.drawString("snoozed", CX, H / 2 - 12);
  spr.setTextSize(1);
  spr.setTextColor(DIM, BG);
  spr.drawString("see you in a bit", CX, H / 2 + 14);
  spr.setTextDatum(TL_DATUM);
  uiPush();
}
