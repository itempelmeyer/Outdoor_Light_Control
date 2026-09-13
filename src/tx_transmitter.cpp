#include "tx_transmitter.h"
#include "rx_receiver.h"
#include "config.h"
#include "rf_protocol.h"
#include "rf_state.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "mqtt_client.h"

namespace
{
    uint8_t nextCounter = 0;

    void sendPulse(
        bool level,
        uint32_t durationUs
    )
    {
        digitalWrite(
            Config::CC1101_GDO0,
            level ? HIGH : LOW
        );

        delayMicroseconds(durationUs);
    }

    void sendBit(bool bit)
    {
        if (!bit)
        {
            // 0 = SHORT HIGH + LONG LOW
            sendPulse(
                true,
                Config::TX_SHORT_US
            );

            sendPulse(
                false,
                Config::TX_LONG_US
            );
        }
        else
        {
            // 1 = LONG HIGH + SHORT LOW
            sendPulse(
                true,
                Config::TX_LONG_US
            );

            sendPulse(
                false,
                Config::TX_SHORT_US
            );
        }
    }

    void sendFrameOnce(
        uint64_t frame
    )
    {
        // 40-bit frame, MSB first.
        for (int bit = 39; bit >= 0; bit--)
        {
            bool value =
                ((frame >> bit) & 0x01) != 0;

            sendBit(value);
        }

        // Inter-frame LOW separator.
        digitalWrite(
            Config::CC1101_GDO0,
            LOW
        );

        delayMicroseconds(
            Config::TX_GAP_US
        );
    }

    void enterTxMode()
    {
        RxReceiver::pauseCapture();

        // Radio still owns GDO0 while in RX.
        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        ELECHOUSE_cc1101.setSidle();

        delayMicroseconds(100);

        // Async TX mode.
        ELECHOUSE_cc1101.setPktFormat(3);

        ELECHOUSE_cc1101.SetTx();

        delayMicroseconds(500);

        // In async TX, ESP32 provides modulation data to GDO0.
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
        // Stop ESP32 from driving GDO0.
        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        delayMicroseconds(100);

        // Force radio idle before reconfiguring.
        ELECHOUSE_cc1101.setSidle();

        delayMicroseconds(100);

        // Re-assert the configuration required by our RX path.
        ELECHOUSE_cc1101.setModulation(2);
        ELECHOUSE_cc1101.setMHZ(
            Config::CC1101_FREQ_MHZ
        );

        ELECHOUSE_cc1101.setPktFormat(3);
        ELECHOUSE_cc1101.setRxBW(203.125);

        // Restore the library's GDO0 association.
        ELECHOUSE_cc1101.setGDO0(
            Config::CC1101_GDO0
        );

        // Back to RX.
        ELECHOUSE_cc1101.SetRx();

        delay(2);

        RxReceiver::resumeCapture();

        Serial.printf(
            "RX resumed, RSSI=%d dBm\n",
            ELECHOUSE_cc1101.getRssi()
        );
    }
}


namespace TxTransmitter
{
    void begin()
    {
        // Receiver normally owns GDO0.
        pinMode(
            Config::CC1101_GDO0,
            INPUT
        );

        Serial.println();
        Serial.println("----- TX INIT -----");
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
        Serial.println("-------------------");
    }


    bool send(
        uint8_t circuit,
        RfProtocol::Action action
    )
    {
        uint64_t frame =
            RfProtocol::buildFrame(
                circuit,
                action,
                nextCounter
            );

        if (frame == 0)
        {
            Serial.println(
                "TX ERROR: invalid circuit/action"
            );

            return false;
        }

        Serial.printf(
            "TX  C%d %-3s  0x%010llX  CNT=%X\n",
            circuit,
            RfProtocol::actionToString(action),
            static_cast<unsigned long long>(
                frame
            ),
            nextCounter
        );

        enterTxMode();

        for (
            uint8_t repeat = 0;
            repeat < Config::TX_REPEAT_COUNT;
            repeat++
        )
        {
            sendFrameOnce(frame);
        }

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

        nextCounter =
            (nextCounter + 1) & 0x0F;

        return true;
    }


    bool sendCircuit1On()
    {
        return send(
            1,
            RfProtocol::Action::On
        );
    }


    bool sendCircuit1Off()
    {
        return send(
            1,
            RfProtocol::Action::Off
        );
    }


    uint8_t getNextCounter()
    {
        return nextCounter;
    }


    void setNextCounter(
        uint8_t counter
    )
    {
        nextCounter =
            counter & 0x0F;

        Serial.printf(
            "TX counter set to %X\n",
            nextCounter
        );
    }
}