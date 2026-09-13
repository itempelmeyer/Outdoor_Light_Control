#pragma once

#include <Arduino.h>

namespace MqttClient
{
    void begin();
    void process();
    bool isConnected();

    void publishCircuitState(
        uint8_t circuit,
        bool isOn
    );
}