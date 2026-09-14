#include "rx_receiver.h"

#include "config.h"
#include "rf_protocol.h"
#include "rf_state.h"
#include "mqtt_client.h"

#include <Arduino.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <sys/time.h>

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

    volatile RawEdge
        edgeBuffer[Config::EDGE_BUFFER_SIZE];

    volatile size_t edgeWriteIndex = 0;
    volatile size_t edgeReadIndex = 0;

    volatile uint32_t lastEdgeMicros = 0;
    volatile uint32_t droppedEdges = 0;

    FramePulse
        framePulses[Config::MAX_FRAME_PULSES];

    size_t framePulseCount = 0;

    int frameRssiPeak = -999;

    bool cc1101Connected = false;
    int cc1101Rssi = -999;

    RxReceiver::ReceivedMessage
        messages[Config::MAX_MESSAGES];

    size_t messageWriteIndex = 0;
    size_t messageCount = 0;
    uint32_t messageSequence = 0;

    uint64_t lastAcceptedRfCode = 0;
    uint32_t lastAcceptedRfMs = 0;

    // -------------------------------------------------------------------------
    // Pulse helpers
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Time
    // -------------------------------------------------------------------------

    uint64_t getEpochMilliseconds()
    {
        struct timeval tv;

        gettimeofday(
            &tv,
            nullptr
        );

        return
            static_cast<uint64_t>(tv.tv_sec) *
                1000ULL +
            static_cast<uint64_t>(tv.tv_usec) /
                1000ULL;
    }

    // -------------------------------------------------------------------------
    // Message history
    // -------------------------------------------------------------------------

    void recordReceivedMessage(
        uint64_t code,
        int rssiDbm
    )
    {
        const RfProtocol::DecodedMessage decoded =
            RfProtocol::decode(code);

        RxReceiver::ReceivedMessage &message =
            messages[messageWriteIndex];

        message.sequence =
            ++messageSequence;

        message.timestampMs =
            getEpochMilliseconds();

        message.code =
            code;

        message.rssiDbm =
            rssiDbm;

        message.circuit =
            decoded.circuit;

        message.action =
            decoded.action;

        // Retained for compatibility with web UI.
        message.counter =
            decoded.counter;

        message.protocolValid =
            decoded.valid;

        messageWriteIndex =
            (messageWriteIndex + 1) %
            Config::MAX_MESSAGES;

        if (
            messageCount <
            Config::MAX_MESSAGES
        )
        {
            messageCount++;
        }
    }

    // -------------------------------------------------------------------------
    // ISR
    // -------------------------------------------------------------------------

    void IRAM_ATTR handleRfEdge()
    {
        const uint32_t now =
            micros();

        const uint32_t duration =
            now - lastEdgeMicros;

        lastEdgeMicros =
            now;

        const bool newLevel =
            digitalRead(
                Config::CC1101_GDO0
            );

        const size_t next =
            (edgeWriteIndex + 1) %
            Config::EDGE_BUFFER_SIZE;

        if (
            next ==
            edgeReadIndex
        )
        {
            droppedEdges++;
            return;
        }

        edgeBuffer[
            edgeWriteIndex
        ].durationUs =
            duration;

        // Duration belongs to the signal level
        // which just ended.
        edgeBuffer[
            edgeWriteIndex
        ].level =
            !newLevel;

        edgeWriteIndex =
            next;
    }

    // -------------------------------------------------------------------------
    // Frame reset
    // -------------------------------------------------------------------------

    void clearFrame()
    {
        framePulseCount = 0;
        frameRssiPeak = -999;
    }

    // -------------------------------------------------------------------------
    // Decode completed Princeton frame
    // -------------------------------------------------------------------------

    void finishFrame()
    {
        // Princeton frame consists of:
        //
        // 48 data pulses
        // = 24 HIGH/LOW pairs
        //
        // plus normally one trailing SHORT HIGH.
        //
        // Captures therefore normally show 49 pulses.

        if (
            framePulseCount < 48
        )
        {
            clearFrame();
            return;
        }

        const int rssi =
            frameRssiPeak;

        // Ignore weak ambient RF.
        if (
            rssi <
            Config::RF_RSSI_THRESHOLD_DBM
        )
        {
            clearFrame();
            return;
        }

        size_t startIndex = 0;

        // Align to first HIGH.
        while (
            startIndex <
                framePulseCount &&
            !framePulses[
                startIndex
            ].level
        )
        {
            startIndex++;
        }

        uint64_t code = 0;

        size_t bitCount = 0;
        size_t invalidCount = 0;

        // Decode only the first 24 valid HIGH/LOW pairs.
        for (
            size_t i = startIndex;
            i + 1 < framePulseCount &&
            bitCount < 24;
            i += 2
        )
        {
            const FramePulse &high =
                framePulses[i];

            const FramePulse &low =
                framePulses[i + 1];

            if (
                !high.level ||
                low.level
            )
            {
                invalidCount++;
                continue;
            }

            bool bit = false;

            // Princeton 0:
            //
            // HIGH short
            // LOW  long
            if (
                isShortPulse(
                    high.durationUs
                ) &&
                isLongPulse(
                    low.durationUs
                )
            )
            {
                bit = false;
            }

            // Princeton 1:
            //
            // HIGH long
            // LOW  short
            else if (
                isLongPulse(
                    high.durationUs
                ) &&
                isShortPulse(
                    low.durationUs
                )
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

        clearFrame();

        // Require exact clean 24-bit packet.
        if (
            bitCount != 24 ||
            invalidCount != 0
        )
        {
            return;
        }

        const uint32_t now =
            millis();

        // Physical remote repeats each packet multiple
        // times while a button is pressed.
        if (
            code ==
                lastAcceptedRfCode &&
            (
                now -
                lastAcceptedRfMs
            ) <
                Config::
                    RF_DUPLICATE_WINDOW_MS
        )
        {
            return;
        }

        lastAcceptedRfCode =
            code;

        lastAcceptedRfMs =
            now;

        const RfProtocol::DecodedMessage decoded =
            RfProtocol::decode(code);

        recordReceivedMessage(
            code,
            rssi
        );

        if (!decoded.valid)
        {
            Serial.printf(
                "RX  UNKNOWN  0x%06llX  RSSI=%d dBm\n",
                static_cast<unsigned long long>(
                    code
                ),
                rssi
            );

            return;
        }

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
            "RX  C%d %-3s  0x%06llX  RSSI=%d dBm\n",
            decoded.circuit,
            RfProtocol::actionToString(
                decoded.action
            ),
            static_cast<unsigned long long>(
                code
            ),
            rssi
        );
    }
}

namespace RxReceiver
{
    void begin()
    {
        Serial.println();
        Serial.println(
            "----- CC1101 INIT -----"
        );

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

        const byte partnum =
            ELECHOUSE_cc1101.SpiReadStatus(
                CC1101_PARTNUM
            );

        const byte version =
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
            partnum == 0x00 &&
            version != 0x00 &&
            version != 0xFF;

        if (!cc1101Connected)
        {
            Serial.println(
                "CC1101: CONNECTION FAILED"
            );

            Serial.println(
                "-----------------------"
            );

            return;
        }

        Serial.println(
            "CC1101: CONNECTION OK"
        );

        // ASK/OOK
        ELECHOUSE_cc1101.setModulation(2);

        ELECHOUSE_cc1101.setMHZ(
            Config::CC1101_FREQ_MHZ
        );

        // Asynchronous raw serial mode.
        ELECHOUSE_cc1101.setPktFormat(3);

        ELECHOUSE_cc1101.setRxBW(
            203.125
        );

        ELECHOUSE_cc1101.SetRx();

        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        lastEdgeMicros =
            micros();

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
            "Protocol: Princeton 24-bit"
        );

        Serial.println(
            "Packet mode: ASYNC RAW"
        );

        Serial.printf(
            "Initial RF RSSI: %d dBm\n",
            cc1101Rssi
        );

        Serial.printf(
            "RSSI threshold: %d dBm\n",
            Config::RF_RSSI_THRESHOLD_DBM
        );

        Serial.printf(
            "RX duplicate window: %lu ms\n",
            static_cast<unsigned long>(
                Config::
                    RF_DUPLICATE_WINDOW_MS
            )
        );

        Serial.println(
            "Expected OFF: 0x256724"
        );

        Serial.println(
            "Expected ON:  0x256728"
        );

        Serial.println(
            "Mode: RX"
        );

        Serial.println(
            "-----------------------"
        );
    }

    void process()
    {
        if (!cc1101Connected)
        {
            return;
        }

        static constexpr size_t
            MAX_EDGES_PER_PASS = 128;

        size_t processed = 0;

        while (
            edgeReadIndex !=
                edgeWriteIndex &&
            processed <
                MAX_EDGES_PER_PASS
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
                (
                    edgeReadIndex +
                    1
                ) %
                Config::EDGE_BUFFER_SIZE;

            interrupts();

            processed++;

            // Refresh RSSI periodically during packet.
            if (
                (processed & 0x07) ==
                1
            )
            {
                cc1101Rssi =
                    ELECHOUSE_cc1101
                        .getRssi();

                if (
                    cc1101Rssi >
                    frameRssiPeak
                )
                {
                    frameRssiPeak =
                        cc1101Rssi;
                }
            }

            // Ignore very small glitches.
            if (
                edge.durationUs <
                100
            )
            {
                continue;
            }

            // Long LOW marks end of packet.
            if (
                !edge.level &&
                edge.durationUs >=
                    Config::FRAME_GAP_US
            )
            {
                if (
                    framePulseCount >
                    0
                )
                {
                    finishFrame();
                }

                continue;
            }

            const bool validPulse =
                isShortPulse(
                    edge.durationUs
                ) ||
                isLongPulse(
                    edge.durationUs
                );

            if (!validPulse)
            {
                continue;
            }

            if (
                framePulseCount <
                Config::
                    MAX_FRAME_PULSES
            )
            {
                framePulses[
                    framePulseCount
                ].level =
                    edge.level;

                framePulses[
                    framePulseCount
                ].durationUs =
                    edge.durationUs;

                framePulseCount++;
            }
            else
            {
                clearFrame();
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

        noInterrupts();

        edgeReadIndex =
            edgeWriteIndex;

        interrupts();

        clearFrame();
    }

    void resumeCapture()
    {
        clearFrame();

        noInterrupts();

        edgeReadIndex =
            edgeWriteIndex;

        interrupts();

        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        delay(2);

        lastEdgeMicros =
            micros();

        attachInterrupt(
            digitalPinToInterrupt(
                Config::CC1101_GDO0
            ),
            handleRfEdge,
            CHANGE
        );

        Serial.println(
            "RX interrupt attached"
        );
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
        if (
            index >=
            messageCount
        )
        {
            return false;
        }

        const size_t bufferIndex =
            (
                messageWriteIndex +
                Config::MAX_MESSAGES -
                messageCount +
                index
            ) %
            Config::MAX_MESSAGES;

        message =
            messages[bufferIndex];

        return true;
    }
}