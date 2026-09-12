#pragma once

#include <Arduino.h>
#include "rf_protocol.h"

namespace RxReceiver
{
    struct ReceivedMessage
    {
        uint32_t sequence;
        uint64_t timestampMs;
        uint64_t code;
        int rssiDbm;

        uint8_t circuit;
        RfProtocol::Action action;
        uint8_t counter;
        bool protocolValid;
    };

    void begin();
    void process();

    bool isConnected();
    int getCurrentRssi();
    uint32_t getDroppedEdges();

    size_t getMessageCount();

    bool getMessage(
        size_t index,
        ReceivedMessage &message
    );

    void pauseCapture();
    void resumeCapture();
}

