#pragma once

#include <Arduino.h>

namespace Config
{
    // -------------------------------------------------------------------------
    // CC1101 / ESP8266 wiring
    // -------------------------------------------------------------------------

    constexpr uint8_t CC1101_GDO0 = 4;   // D2
    constexpr uint8_t CC1101_CS   = 5;   // D1
    constexpr uint8_t CC1101_SCK  = 14;  // D5
    constexpr uint8_t CC1101_MOSI = 13;  // D7
    constexpr uint8_t CC1101_MISO = 12;  // D6

    // -------------------------------------------------------------------------
    // RF configuration
    // -------------------------------------------------------------------------

    constexpr float CC1101_FREQ_MHZ = 433.920f;

    // Confirmed Princeton-style pulse timing.
    //
    // Typical measured values:
    // short ~= 320 us
    // long  ~= 960 us
    //
    // Give RX some margin around the measured values.

    constexpr uint32_t SHORT_MIN_US = 240;
    constexpr uint32_t SHORT_MAX_US = 420;

    constexpr uint32_t LONG_MIN_US = 820;
    constexpr uint32_t LONG_MAX_US = 1120;

    // Measured separator around 9.85-9.89 ms.
    constexpr uint32_t FRAME_GAP_US = 8000;

    // Ignore very weak packets/noise.
    constexpr int RF_RSSI_THRESHOLD_DBM = -72;

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------

    constexpr size_t EDGE_BUFFER_SIZE = 512;
    constexpr size_t MAX_FRAME_PULSES = 128;
    constexpr size_t MAX_MESSAGES = 50;

    // -------------------------------------------------------------------------
    // RX duplicate suppression
    // -------------------------------------------------------------------------

    // Physical remote repeats each frame several times during one button press.
    constexpr uint32_t RF_DUPLICATE_WINDOW_MS = 500;

    // -------------------------------------------------------------------------
    // TX timings
    // -------------------------------------------------------------------------

    constexpr uint32_t TX_SHORT_US = 320;
    constexpr uint32_t TX_LONG_US  = 960;
    constexpr uint32_t TX_GAP_US   = 9870;

    // Send enough repeats to emulate the physical remote reliably.
    constexpr uint8_t TX_REPEAT_COUNT = 8;

    // -------------------------------------------------------------------------
    // Network / web
    // -------------------------------------------------------------------------

    constexpr char HOSTNAME[] = "outdoorlightcontrol";

    constexpr char TZ_INFO[] =
        "CST6CDT,M3.2.0/2,M11.1.0/2";

    constexpr char NTP_SERVER_1[] =
        "pool.ntp.org";

    constexpr char NTP_SERVER_2[] =
        "time.nist.gov";
}