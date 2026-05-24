// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#pragma once
#include <stdint.h>

// Pages cycled by short-press A in interactive mode.
enum Page { PAGE_HOME = 0, PAGE_STATS, PAGE_BATTERY, PAGE_COUNT };

// Settings menu items. Keep in sync with uiMenuItemLabel() in ui.cpp.
// Menu pages — the settings list is split for legibility.
enum MenuPage { MP_REMINDER = 0, MP_DEVICE, MP_COUNT };

// Catalog of menu items. The order here doesn't matter — pages reference
// items by ID via PAGE_ITEMS in ui.cpp.
enum MenuItem {
  MI_INTERVAL = 0,
  MI_ANCHOR_HOUR,
  MI_ANCHOR_MIN,
  MI_QUIET_START,
  MI_QUIET_END,
  MI_PULSE_RHYTHM,
  MI_PULSE_LEN,
  MI_VOLUME,
  MI_PULSE_PREVIEW,
  MI_BRIGHTNESS,
  MI_SOUND,
  MI_LED,
  MI_AUTO_SLEEP,
  MI_WAKE_MOTION,
  MI_SET_HOUR,
  MI_SET_MINUTE,
  MI_SHUTDOWN,
  MI_NEXT_PAGE,
  MI_EXIT,
  MI_ITEM_COUNT
};

// Resolve a (page, slot) coordinate to a MenuItem id, or 0xFF if out of range.
uint8_t menuItemAt(uint8_t page, uint8_t sel);

// Number of slots on the given page.
uint8_t menuPageLen(uint8_t page);

// Header label for the page ("REMINDER" / "DEVICE").
const char* menuPageName(uint8_t page);

// Implemented in main.cpp. Lets the menu renderer show "really?" while the
// shutdown item is in its tap-twice-to-confirm armed window.
bool menuShutdownArmed();

void uiInit();
void uiSetBrightness(uint8_t level);

void uiDrawSplash();
void uiDrawPage(Page p);
void uiDrawMenu(uint8_t page, uint8_t selected);

// Reminder screen. elapsedMs runs 0..totalMs; pulseActive=true to flash the
// drop white briefly with the buzz.
void uiDrawReminder(uint32_t elapsedMs, uint32_t totalMs, bool pulseActive);

void uiDrawAck();             // post-acknowledge "great!" splash
void uiDrawTimeout();         // shown briefly after 3-min timeout before sleep
void uiPush();                // sprite → LCD; call once per frame

// Helper formatters used by both pages and menu.
const char* uiMenuItemLabel(uint8_t i);
void uiFormatMenuValue(uint8_t i, char* out, uint8_t outSize);
