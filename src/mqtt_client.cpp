#include "mqtt_client.h"

#include "rf_state.h"
#include "secrets.h"
#include "tx_transmitter.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

namespace
{
    WiFiClient wifiClient;
    PubSubClient mqttClient(wifiClient);

    uint32_t lastReconnectAttemptMs = 0;

    constexpr char MQTT_CLIENT_ID[] =
        "rfhouse-esp32";

    constexpr char TOPIC_STATUS[] =
        "rfhouse/status";

    constexpr char TOPIC_C1_SET[] =
        "rfhouse/circuit1/set";

    constexpr char TOPIC_C1_STATE[] =
        "rfhouse/circuit1/state";


    void publishCurrentState()
    {
        RfState::CircuitState state =
            RfState::getState(1);

        if (!state.known)
            return;

        mqttClient.publish(
            TOPIC_C1_STATE,
            state.isOn ? "ON" : "OFF",
            true
        );
    }


    void handleMessage(
        char *topic,
        byte *payload,
        unsigned int length
    )
    {
        String message;

        for (
            unsigned int i = 0;
            i < length;
            i++
        )
        {
            message +=
                static_cast<char>(
                    payload[i]
                );
        }

        message.trim();
        message.toUpperCase();

        Serial.printf(
            "MQTT RX  topic=%s  payload=%s\n",
            topic,
            message.c_str()
        );

        if (
            String(topic) ==
            TOPIC_C1_SET
        )
        {
            if (message == "ON")
            {
                TxTransmitter::sendCircuit1On();
            }
            else if (message == "OFF")
            {
                TxTransmitter::sendCircuit1Off();
            }
        }
    }


    bool connectMqtt()
    {
        Serial.printf(
            "Connecting to MQTT broker %s:%d...\n",
            MQTT_HOST,
            MQTT_PORT
        );

        bool connected =
            mqttClient.connect(
                MQTT_CLIENT_ID,
                MQTT_USERNAME,
                MQTT_PASSWORD,
                TOPIC_STATUS,
                0,
                true,
                "offline"
            );

        if (!connected)
        {
            Serial.printf(
                "MQTT connection failed. state=%d\n",
                mqttClient.state()
            );

            return false;
        }

        Serial.println(
            "MQTT connected successfully."
        );

        mqttClient.publish(
            TOPIC_STATUS,
            "online",
            true
        );

        mqttClient.subscribe(
            TOPIC_C1_SET
        );

        Serial.printf(
            "MQTT subscribed: %s\n",
            TOPIC_C1_SET
        );

        publishCurrentState();

        return true;
    }
}


namespace MqttClient
{
    void begin()
    {
        mqttClient.setServer(
            MQTT_HOST,
            MQTT_PORT
        );

        mqttClient.setCallback(
            handleMessage
        );

        connectMqtt();
    }


    void process()
    {
        if (
            WiFi.status() != WL_CONNECTED
        )
        {
            return;
        }

        if (!mqttClient.connected())
        {
            if (
                millis() -
                lastReconnectAttemptMs >=
                5000
            )
            {
                lastReconnectAttemptMs =
                    millis();

                connectMqtt();
            }

            return;
        }

        mqttClient.loop();
    }


    bool isConnected()
    {
        return mqttClient.connected();
    }


    void publishCircuitState(
        uint8_t circuit,
        bool isOn
    )
    {
        if (!mqttClient.connected())
            return;

        if (circuit != 1)
            return;

        mqttClient.publish(
            TOPIC_C1_STATE,
            isOn ? "ON" : "OFF",
            true
        );

        Serial.printf(
            "MQTT TX  topic=%s  payload=%s\n",
            TOPIC_C1_STATE,
            isOn ? "ON" : "OFF"
        );
    }
}