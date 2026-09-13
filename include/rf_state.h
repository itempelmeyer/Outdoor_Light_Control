#pragma once

#include <Arduino.h>
#include "rf_protocol.h"

namespace RfState
{
    enum class Source
    {
        Unknown,
        Rx,
        Tx
    };

    struct CircuitState
    {
        bool known;
        bool isOn;
        Source source;
        uint32_t lastUpdateMs;
    };

    void setState(
        uint8_t circuit,
        RfProtocol::Action action,
        Source source
    );

    CircuitState getState(
        uint8_t circuit
    );

    const char *sourceToString(
        Source source
    );
}