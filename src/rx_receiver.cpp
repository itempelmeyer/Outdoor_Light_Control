#include "rx_receiver.h"
#include "config.h"
#include "rf_protocol.h"
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <sys/time.h>
#include "rf_state.h"
#include "mqtt_client.h"

namespace
{
    struct RawEdge
    {
        uint32_t durationUs;
        bool level;
    };

    struct FramePulse
    {
        bool level;
        uint32_t durationUs;
    };

    volatile RawEdge edgeBuffer[Config::EDGE_BUFFER_SIZE];
    volatile size_t edgeWriteIndex = 0;
    volatile size_t edgeReadIndex = 0;
    volatile uint32_t lastEdgeMicros = 0;
    volatile uint32_t droppedEdges = 0;

    FramePulse framePulses[Config::MAX_FRAME_PULSES];
    size_t framePulseCount = 0;
    int frameRssiPeak = -999;

    bool cc1101Connected = false;
    int cc1101Rssi = -999;

    RxReceiver::ReceivedMessage messages[Config::MAX_MESSAGES];
    size_t messageWriteIndex = 0;
    size_t messageCount = 0;
    uint32_t messageSequence = 0;

    uint64_t lastPrintedRfCode = 0;
    uint32_t lastPrintedRfMs = 0;

    bool isShortPulse(uint32_t durationUs)
    {
        return
            durationUs >= Config::SHORT_MIN_US &&
            durationUs <= Config::SHORT_MAX_US;
    }

    bool isLongPulse(uint32_t durationUs)
    {
        return
            durationUs >= Config::LONG_MIN_US &&
            durationUs <= Config::LONG_MAX_US;
    }

    uint64_t getEpochMilliseconds()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);

        return
            static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
            static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
    }
    void recordReceivedMessage(
        uint64_t code,
        int rssiDbm
    )
    {
        RfProtocol::DecodedMessage decoded =
            RfProtocol::decode(code);

        RxReceiver::ReceivedMessage &message =
            messages[messageWriteIndex];

        message.sequence = ++messageSequence;
        message.timestampMs = getEpochMilliseconds();
        message.code = code;
        message.rssiDbm = rssiDbm;

        message.circuit = decoded.circuit;
        message.action = decoded.action;
        message.counter = decoded.counter;
        message.protocolValid = decoded.valid;

        messageWriteIndex =
            (messageWriteIndex + 1) %
            Config::MAX_MESSAGES;

        if (messageCount < Config::MAX_MESSAGES)
        {
            messageCount++;
        }
    }

    void ICACHE_RAM_ATTR handleRfEdge()
    {
        uint32_t now = micros();

        uint32_t duration =
            now - lastEdgeMicros;

        lastEdgeMicros = now;

        bool level =
            digitalRead(Config::CC1101_GDO0);

        size_t next =
            (edgeWriteIndex + 1) %
            Config::EDGE_BUFFER_SIZE;

        if (next == edgeReadIndex)
        {
            droppedEdges++;
            return;
        }

        edgeBuffer[edgeWriteIndex].durationUs =
            duration;

        // Duration belongs to the level that just ended.
        edgeBuffer[edgeWriteIndex].level =
            !level;

        edgeWriteIndex = next;
    }

    void finishFrame()
    {
        if (framePulseCount < 4)
        {
            framePulseCount = 0;
            frameRssiPeak = -999;
            return;
        }

        uint64_t code = 0;
        size_t bitCount = 0;
        size_t invalidCount = 0;

        size_t startIndex = 0;

        // Align to the first HIGH pulse.
        while (
            startIndex < framePulseCount &&
            !framePulses[startIndex].level
        )
        {
            startIndex++;
        }

        for (
            size_t i = startIndex;
            i + 1 < framePulseCount;
            i += 2
        )
        {
            const FramePulse &high =
                framePulses[i];

            const FramePulse &low =
                framePulses[i + 1];

            if (!high.level || low.level)
            {
                invalidCount++;
                continue;
            }

            bool bit;

            // 0 = SHORT HIGH + LONG LOW
            if (
                isShortPulse(high.durationUs) &&
                isLongPulse(low.durationUs)
            )
            {
                bit = false;
            }
            // 1 = LONG HIGH + SHORT LOW
            else if (
                isLongPulse(high.durationUs) &&
                isShortPulse(low.durationUs)
            )
            {
                bit = true;
            }
            else
            {
                invalidCount++;
                continue;
            }

            code <<= 1;

            if (bit)
            {
                code |= 1ULL;
            }

            bitCount++;
        }

        const int rssi = frameRssiPeak;

        framePulseCount = 0;
        frameRssiPeak = -999;

        // Only accept clean 40-bit frames.
        if (
            bitCount != 40 ||
            invalidCount != 0
        )
        {
            return;
        }

        const uint32_t now = millis();

        // Remote repeats the same message several times
        // during one physical button press.
        if (
            code == lastPrintedRfCode &&
            (now - lastPrintedRfMs) <
                Config::RF_DUPLICATE_WINDOW_MS
        )
        {
            return;
        }

        lastPrintedRfCode = code;
        lastPrintedRfMs = now;

        RfProtocol::DecodedMessage decoded =
    RfProtocol::decode(code);

        recordReceivedMessage(
            code,
            rssi
        );

        if (decoded.valid)
        {
            RfState::setState(
                decoded.circuit,
                decoded.action,
                RfState::Source::Rx
            );

            MqttClient::publishCircuitState(
                decoded.circuit,
                decoded.action ==
                    RfProtocol::Action::On
            );

            Serial.printf(
                "RX  C%d %-3s  0x%010llX  CNT=%X  RSSI=%d dBm\n",
                decoded.circuit,
                RfProtocol::actionToString(
                    decoded.action
                ),
                static_cast<unsigned long long>(
                    code
                ),
                decoded.counter,
                rssi
            );
        }
        else
        {
            Serial.printf(
                "RX  UNKNOWN  0x%010llX  RSSI=%d dBm\n",
                static_cast<unsigned long long>(
                    code
                ),
                rssi
            );
        }
    }

}


namespace RxReceiver
{
    void begin()
    {
        Serial.println();
        Serial.println("----- CC1101 INIT -----");

        ELECHOUSE_cc1101.setSpiPin(
            Config::CC1101_SCK,
            Config::CC1101_MISO,
            Config::CC1101_MOSI,
            Config::CC1101_CS
        );

        ELECHOUSE_cc1101.setGDO0(
            Config::CC1101_GDO0
        );

        Serial.printf(
            "SPI: SCK=%d MISO=%d MOSI=%d CS=%d\n",
            Config::CC1101_SCK,
            Config::CC1101_MISO,
            Config::CC1101_MOSI,
            Config::CC1101_CS
        );

        Serial.printf(
            "GDO0: GPIO%d\n",
            Config::CC1101_GDO0
        );

        ELECHOUSE_cc1101.Init();

        byte partnum =
            ELECHOUSE_cc1101.SpiReadStatus(
                CC1101_PARTNUM
            );

        byte version =
            ELECHOUSE_cc1101.SpiReadStatus(
                CC1101_VERSION
            );

        Serial.printf(
            "PARTNUM: 0x%02X\n",
            partnum
        );

        Serial.printf(
            "VERSION: 0x%02X\n",
            version
        );

        cc1101Connected =
            (partnum == 0x00) &&
            (version != 0x00) &&
            (version != 0xFF);

        if (!cc1101Connected)
        {
            Serial.println(
                "CC1101: CONNECTION FAILED"
            );
            Serial.println("-----------------------");
            return;
        }

        Serial.println(
            "CC1101: CONNECTION OK"
        );

        ELECHOUSE_cc1101.setModulation(2);
        ELECHOUSE_cc1101.setMHZ(
            Config::CC1101_FREQ_MHZ
        );
        ELECHOUSE_cc1101.setPktFormat(3);
        ELECHOUSE_cc1101.setRxBW(203.125);
        ELECHOUSE_cc1101.SetRx();

        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        lastEdgeMicros = micros();

        attachInterrupt(
            digitalPinToInterrupt(
                Config::CC1101_GDO0
            ),
            handleRfEdge,
            CHANGE
        );

        delay(10);

        cc1101Rssi =
            ELECHOUSE_cc1101.getRssi();

        Serial.printf(
            "Frequency: %.3f MHz\n",
            Config::CC1101_FREQ_MHZ
        );

        Serial.println(
            "Modulation: ASK/OOK"
        );
        Serial.println(
            "Packet mode: ASYNC RAW"
        );

        Serial.printf(
            "Initial RF RSSI: %d dBm\n",
            cc1101Rssi
        );

        Serial.printf(
            "RX duplicate window: %lu ms\n",
            static_cast<unsigned long>(
                Config::RF_DUPLICATE_WINDOW_MS
            )
        );

        Serial.println("Mode: RX");
        Serial.println("-----------------------");
    }

    void process()
    {
        if (!cc1101Connected)
        {
            return;
        }

        cc1101Rssi =
            ELECHOUSE_cc1101.getRssi();

        if (cc1101Rssi > frameRssiPeak)
        {
            frameRssiPeak = cc1101Rssi;
        }

        static constexpr size_t
            MAX_EDGES_PER_PASS = 128;

        size_t processed = 0;

        while (
            edgeReadIndex != edgeWriteIndex &&
            processed < MAX_EDGES_PER_PASS
        )
        {
            RawEdge edge;

            noInterrupts();

            edge.durationUs =
                edgeBuffer[
                    edgeReadIndex
                ].durationUs;

            edge.level =
                edgeBuffer[
                    edgeReadIndex
                ].level;

            edgeReadIndex =
                (edgeReadIndex + 1) %
                Config::EDGE_BUFFER_SIZE;

            interrupts();

            processed++;

            if (
                edge.durationUs <
                Config::SHORT_MIN_US
            )
            {
                continue;
            }

            // Long LOW = frame separator.
            if (
                !edge.level &&
                edge.durationUs >=
                    Config::FRAME_GAP_US
            )
            {
                if (framePulseCount > 0)
                {
                    finishFrame();
                }

                continue;
            }

            const bool validPulse =
                isShortPulse(edge.durationUs) ||
                isLongPulse(edge.durationUs);

            if (!validPulse)
            {
                continue;
            }

            if (
                framePulseCount <
                Config::MAX_FRAME_PULSES
            )
            {
                framePulses[
                    framePulseCount
                ].level = edge.level;

                framePulses[
                    framePulseCount
                ].durationUs =
                    edge.durationUs;

                framePulseCount++;
            }
            else
            {
                framePulseCount = 0;
                frameRssiPeak = -999;
            }
        }
    }

    void pauseCapture()
        {
            detachInterrupt(
                digitalPinToInterrupt(
                    Config::CC1101_GDO0
                )
            );

            // Throw away anything partially received.
            noInterrupts();

            edgeReadIndex = edgeWriteIndex;

            interrupts();

            framePulseCount = 0;
            frameRssiPeak = -999;
        }

    void resumeCapture()
    {
        framePulseCount = 0;
        frameRssiPeak = -999;

        noInterrupts();

        edgeReadIndex = edgeWriteIndex;

        interrupts();

        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        delay(2);

        lastEdgeMicros = micros();

        attachInterrupt(
            digitalPinToInterrupt(
                Config::CC1101_GDO0
            ),
            handleRfEdge,
            CHANGE
        );

        Serial.println("RX interrupt attached");
    }

    bool isConnected()
    {
        return cc1101Connected;
    }

    int getCurrentRssi()
    {
        return cc1101Rssi;
    }

    uint32_t getDroppedEdges()
    {
        return droppedEdges;
    }

    size_t getMessageCount()
    {
        return messageCount;
    }

    bool getMessage(
        size_t index,
        ReceivedMessage &message
    )
    {
        if (index >= messageCount)
        {
            return false;
        }

        size_t bufferIndex =
            (
                messageWriteIndex +
                Config::MAX_MESSAGES -
                messageCount +
                index
            ) % Config::MAX_MESSAGES;

        message = messages[bufferIndex];

        return true;
    }
}