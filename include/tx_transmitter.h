#pragma once

#include <Arduino.h>
#include "rf_protocol.h"

namespace TxTransmitter
{
    void begin();

    bool sendCircuit1On();
    bool sendCircuit1Off();

    // Compatibility only; not part of the RF protocol.
    uint8_t getNextCounter();
}