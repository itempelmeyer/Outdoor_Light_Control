#pragma once

#include <Arduino.h>

namespace RfProtocol
{
    enum class Action
    {
        Unknown,
        Off,
        On
    };

    struct DecodedMessage
    {
        bool valid;

        uint16_t device;
        uint16_t command;

        // Kept for compatibility with existing UI/state code.
        uint8_t counter;
        uint8_t checkNibble;

        uint8_t circuit;
        Action action;
    };

    constexpr uint32_t CODE_CIRCUIT1_OFF = 0x256724;
    constexpr uint32_t CODE_CIRCUIT1_ON  = 0x256728;

    DecodedMessage decode(uint64_t code);

    const char *actionToString(Action action);

    uint8_t buildCounterByte(uint8_t counter);

    uint64_t buildFrame(
        uint8_t circuit,
        Action action,
        uint8_t counter = 0
    );
}