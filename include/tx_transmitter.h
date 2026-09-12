#pragma once

#include <Arduino.h>
#include "rf_protocol.h"

namespace TxTransmitter
{
    void begin();

    bool send(
        uint8_t circuit,
        RfProtocol::Action action
    );

    bool sendCircuit1On();
    bool sendCircuit1Off();

    uint8_t getNextCounter();
    void setNextCounter(uint8_t counter);
}