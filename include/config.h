#pragma once
#include <Arduino.h>

namespace Config
{
    // ----- CC1101 / SPI -----
    // constexpr uint8_t CC1101_GDO0 = 33;
    // constexpr uint8_t CC1101_CS   = 17;
    // constexpr uint8_t CC1101_SCK  = 22;
    // constexpr uint8_t CC1101_MOSI = 23;
    // constexpr uint8_t CC1101_MISO = 32;

    constexpr uint8_t CC1101_GDO0 = 4;   // D2
    constexpr uint8_t CC1101_CS   = 5;   // D1
    constexpr uint8_t CC1101_SCK  = 14;  // D5
    constexpr uint8_t CC1101_MOSI = 13;  // D7
    constexpr uint8_t CC1101_MISO = 12;  // D6

    constexpr float CC1101_FREQ_MHZ = 433.945f;

    // ----- RF decoder timing -----
    constexpr uint32_t SHORT_MIN_US = 200;
    constexpr uint32_t SHORT_MAX_US = 450;
    constexpr uint32_t LONG_MIN_US  = 700;
    constexpr uint32_t LONG_MAX_US  = 1050;
    constexpr uint32_t FRAME_GAP_US = 3000;

    constexpr size_t EDGE_BUFFER_SIZE = 512;
    constexpr size_t MAX_FRAME_PULSES = 128;
    constexpr size_t MAX_MESSAGES = 50;

    // ----- RF transmit timing -----
    constexpr uint32_t TX_SHORT_US = 300;
    constexpr uint32_t TX_LONG_US  = 900;
    constexpr uint32_t TX_GAP_US   = 4400;

    constexpr uint8_t TX_REPEAT_COUNT = 8;

    // Suppress repeated copies transmitted during one button press.
    constexpr uint32_t RF_DUPLICATE_WINDOW_MS = 500;

    // ----- Network / web -----
    constexpr char HOSTNAME[] = "local433signals";

    constexpr char TZ_INFO[] =
        "CST6CDT,M3.2.0/2,M11.1.0/2";

    constexpr char NTP_SERVER_1[] = "pool.ntp.org";
    constexpr char NTP_SERVER_2[] = "time.nist.gov";
}
