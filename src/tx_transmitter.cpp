#include "tx_transmitter.h"

#include "config.h"
#include "rf_protocol.h"
#include "rf_state.h"
#include "rx_receiver.h"
#include "mqtt_client.h"

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace
{
    // Kept only for compatibility with the existing web UI.
    // The Princeton protocol itself does NOT use a rolling counter.
    uint8_t compatibilityCounter = 0;

    void enterTxMode()
    {
        RxReceiver::pauseCapture();

        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        ELECHOUSE_cc1101.setSidle();

        delayMicroseconds(100);

        ELECHOUSE_cc1101.setModulation(2);

        ELECHOUSE_cc1101.setMHZ(
            Config::CC1101_FREQ_MHZ
        );

        ELECHOUSE_cc1101.setPktFormat(3);

        ELECHOUSE_cc1101.SetTx();

        delayMicroseconds(500);

        pinMode(
            Config::CC1101_GDO0,
            OUTPUT
        );

        digitalWrite(
            Config::CC1101_GDO0,
            LOW
        );
    }

    void returnToRxMode()
    {
        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        delayMicroseconds(100);

        ELECHOUSE_cc1101.setSidle();

        delayMicroseconds(100);

        ELECHOUSE_cc1101.setModulation(2);

        ELECHOUSE_cc1101.setMHZ(
            Config::CC1101_FREQ_MHZ
        );

        ELECHOUSE_cc1101.setPktFormat(3);

        ELECHOUSE_cc1101.setRxBW(
            203.125
        );

        ELECHOUSE_cc1101.setGDO0(
            Config::CC1101_GDO0
        );

        ELECHOUSE_cc1101.SetRx();

        delay(2);

        RxReceiver::resumeCapture();

        Serial.printf(
            "RX resumed, RSSI=%d dBm\n",
            ELECHOUSE_cc1101.getRssi()
        );
    }

    inline void txHigh(
        uint32_t durationUs
    )
    {
        digitalWrite(
            Config::CC1101_GDO0,
            HIGH
        );

        delayMicroseconds(
            durationUs
        );
    }

    inline void txLow(
        uint32_t durationUs
    )
    {
        digitalWrite(
            Config::CC1101_GDO0,
            LOW
        );

        delayMicroseconds(
            durationUs
        );
    }

    void transmitBit(bool bit)
    {
        if (bit)
        {
            // Princeton 1:
            // HIGH long, LOW short
            txHigh(
                Config::TX_LONG_US
            );

            txLow(
                Config::TX_SHORT_US
            );
        }
        else
        {
            // Princeton 0:
            // HIGH short, LOW long
            txHigh(
                Config::TX_SHORT_US
            );

            txLow(
                Config::TX_LONG_US
            );
        }
    }

    void transmitFrame(
        uint32_t code
    )
    {
        // Send 24 bits, MSB first.
        for (
            int bit = 23;
            bit >= 0;
            bit--
        )
        {
            transmitBit(
                (
                    code >> bit
                ) & 0x01
            );
        }

        // Trailing sync pulse confirmed from capture:
        // HIGH ~320 us
        // LOW  ~9870 us

        txHigh(
            Config::TX_SHORT_US
        );

        txLow(
            Config::TX_GAP_US
        );
    }

    bool sendCommand(
        uint8_t circuit,
        RfProtocol::Action action
    )
    {
        const uint64_t frame =
            RfProtocol::buildFrame(
                circuit,
                action,
                0
            );

        if (frame == 0)
        {
            Serial.println(
                "TX requested invalid RF command"
            );

            return false;
        }

        const uint32_t code =
            static_cast<uint32_t>(
                frame
            );

        Serial.printf(
            "TX  C%d %-3s  0x%06lX\n",
            circuit,
            RfProtocol::actionToString(
                action
            ),
            static_cast<unsigned long>(
                code
            )
        );

        enterTxMode();

        for (
            uint8_t repeat = 0;
            repeat <
                Config::TX_REPEAT_COUNT;
            repeat++
        )
        {
            transmitFrame(
                code
            );

            yield();
        }

        digitalWrite(
            Config::CC1101_GDO0,
            LOW
        );

        returnToRxMode();

        RfState::setState(
            circuit,
            action,
            RfState::Source::Tx
        );

        MqttClient::publishCircuitState(
            circuit,
            action ==
                RfProtocol::Action::On
        );

        compatibilityCounter++;

        return true;
    }
}

namespace TxTransmitter
{
    void begin()
    {
        Serial.println();
        Serial.println(
            "----- TX INIT -----"
        );

        Serial.printf(
            "GDO0 TX/RX pin: GPIO%d\n",
            Config::CC1101_GDO0
        );

        Serial.printf(
            "TX short pulse: %lu us\n",
            static_cast<unsigned long>(
                Config::TX_SHORT_US
            )
        );

        Serial.printf(
            "TX long pulse:  %lu us\n",
            static_cast<unsigned long>(
                Config::TX_LONG_US
            )
        );

        Serial.printf(
            "TX frame gap:   %lu us\n",
            static_cast<unsigned long>(
                Config::TX_GAP_US
            )
        );

        Serial.printf(
            "TX repeats:     %u\n",
            Config::TX_REPEAT_COUNT
        );

        Serial.println(
            "OFF code:       0x256724"
        );

        Serial.println(
            "ON code:        0x256728"
        );

        Serial.println(
            "-------------------"
        );
    }

    bool sendCircuit1On()
    {
        return sendCommand(
            1,
            RfProtocol::Action::On
        );
    }

    bool sendCircuit1Off()
    {
        return sendCommand(
            1,
            RfProtocol::Action::Off
        );
    }

    uint8_t getNextCounter()
    {
        // Compatibility only.
        // Not transmitted in the Princeton packet.
        return compatibilityCounter;
    }
}