// m5stack-water-reminder — hydration reminder firmware for M5StickC Plus
// Copyright (c) 2026 Ramy Ezzat
// SPDX-License-Identifier: MIT

#include "timesync.hpp"
#include <M5StickCPlus.h>
#include <WiFi.h>
#include "esp_sntp.h"

#if __has_include("secrets.hpp")
    #include "secrets.hpp"
#endif
#ifndef WIFI_SSID
    #warning "src/secrets.hpp not found (copy secrets.hpp.example) - NTP sync disabled"
    #define WIFI_SSID ""
    #define WIFI_PASS ""
#endif

// Eastern European Time: UTC+2, DST to UTC+3 last Sun of March 03:00 →
// last Sun of October 04:00 (EU rule). POSIX TZ, so DST is automatic.
static const char* TZ_RULE = "EET-2EEST,M3.5.0/3,M10.5.0/4";
static const char* NTP_1   = "pool.ntp.org";
static const char* NTP_2   = "time.google.com";

// Overall budget for WiFi join + NTP response. A healthy join takes 2-4 s;
// past 10 s we're out of range / wrong password and burning battery.
static const uint32_t SYNC_BUDGET_MS = 10000;

static bool     active     = false;
static bool     sntpKicked = false;
static uint32_t startMs    = 0;

static void wifiOff() {
    WiFi.disconnect(true /*wifioff*/);
    WiFi.mode(WIFI_OFF);
}

void timeSyncBegin() {
    if (active) return;
    if (WIFI_SSID[0] == '\0') return;   // unconfigured — stay WiFi-less
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    startMs    = millis();
    sntpKicked = false;
    active     = true;
}

TimeSyncState timeSyncPoll() {
    if (!active) return TS_IDLE;

    if (millis() - startMs > SYNC_BUDGET_MS) {
        wifiOff();
        active = false;
        return TS_FAILED;
    }

    if (WiFi.status() != WL_CONNECTED) return TS_RUNNING;

    if (!sntpKicked) {
        // configTzTime (re)starts SNTP; sync status drops to RESET and only
        // reaches COMPLETED once a server actually answered *this boot* — so a
        // stale system clock surviving from before can't fake a fresh sync.
        configTzTime(TZ_RULE, NTP_1, NTP_2);
        sntpKicked = true;
        return TS_RUNNING;
    }

    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) return TS_RUNNING;

    struct tm t;
    if (!getLocalTime(&t, 0)) return TS_RUNNING;

    // Write local time into the BM8563 — the firmware's single time source.
    RTC_TimeTypeDef tm;
    tm.Hours   = t.tm_hour;
    tm.Minutes = t.tm_min;
    tm.Seconds = t.tm_sec;
    RTC_DateTypeDef dt;
    dt.Year    = t.tm_year + 1900;
    dt.Month   = t.tm_mon + 1;
    dt.Date    = t.tm_mday;
    dt.WeekDay = t.tm_wday;
    M5.Rtc.SetTime(&tm);
    M5.Rtc.SetDate(&dt);

    wifiOff();
    active = false;
    return TS_DONE;
}

bool timeSyncFinish() {
    timeSyncBegin();                     // no-op if already running/unconfigured
    for (;;) {
        TimeSyncState s = timeSyncPoll();
        if (s == TS_DONE)  return true;
        if (s != TS_RUNNING) return false; // TS_IDLE (unconfigured) or TS_FAILED
        delay(50);
    }
}
