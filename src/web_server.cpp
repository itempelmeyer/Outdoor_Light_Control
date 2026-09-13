#include "web_server.h"

#include "config.h"
#include "rx_receiver.h"
#include "web_page.h"
#include "secrets.h"
#include "tx_transmitter.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "rf_state.h"

namespace
{
    WebServer server(80);

    bool isTimeSynchronized()
    {
        time_t now = time(nullptr);

        return now > 1704067200;
    }

    String getFormattedTime()
    {
        struct tm timeinfo;

        if (!getLocalTime(&timeinfo, 10))
        {
            return "NOT_SYNCED";
        }

        char buffer[32];

        strftime(
            buffer,
            sizeof(buffer),
            "%Y-%m-%d %H:%M:%S",
            &timeinfo
        );

        return String(buffer);
    }

    void connectWifi()
    {
        Serial.println();
        Serial.println(
            "Connecting to Wi-Fi..."
        );

        Serial.printf(
            "SSID: %s\n",
            WIFI_SSID
        );

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(Config::HOSTNAME);

        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );

        uint32_t startTime = millis();

        while (
            WiFi.status() != WL_CONNECTED
        )
        {
            delay(500);
            Serial.print(".");

            if (
                millis() - startTime >
                30000
            )
            {
                Serial.println();
                Serial.println(
                    "Wi-Fi connection timeout."
                );
                ESP.restart();
            }
        }

        Serial.println();
        Serial.println(
            "Wi-Fi connected."
        );

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.print("RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    }

    void configureTime()
    {
        Serial.println(
            "Starting NTP synchronization..."
        );

        configTzTime(
            Config::TZ_INFO,
            Config::NTP_SERVER_1,
            Config::NTP_SERVER_2
        );

        uint32_t startTime = millis();

        while (!isTimeSynchronized())
        {
            delay(250);
            Serial.print(".");

            if (
                millis() - startTime >
                15000
            )
            {
                Serial.println();
                Serial.println(
                    "NTP synchronization timed out."
                );
                return;
            }
        }

        Serial.println();

        Serial.print(
            "Time synchronized: "
        );
        Serial.println(
            getFormattedTime()
        );
    }

    void configureMdns()
    {
        if (!MDNS.begin(Config::HOSTNAME))
        {
            Serial.println(
                "ERROR: Failed to start mDNS."
            );
            return;
        }

        MDNS.addService(
            "http",
            "tcp",
            80
        );

        Serial.printf(
            "mDNS started: http://%s.local\n",
            Config::HOSTNAME
        );
    }

    String buildStatusJson()
    {
        String json;
        json.reserve(8000);

        json += "{";

        json += "\"hostname\":\"";
        json += Config::HOSTNAME;
        json += "\",";

        json += "\"time\":\"";
        json += getFormattedTime();
        json += "\",";

        json += "\"time_synced\":";
        json +=
            isTimeSynchronized()
            ? "true"
            : "false";
        json += ",";

        json += "\"uptime_ms\":";
        json += String(millis());
        json += ",";

        json += "\"wifi\":{";

        json += "\"ssid\":\"";
        json += WiFi.SSID();
        json += "\",";

        json += "\"ip\":\"";
        json +=
            WiFi.localIP().toString();
        json += "\",";

        json += "\"rssi_dbm\":";
        json += String(WiFi.RSSI());

        json += "},";

        json += "\"rf\":{";

        json += "\"connected\":";
        json +=
            RxReceiver::isConnected()
            ? "true"
            : "false";
        json += ",";

        json += "\"frequency_mhz\":";
        json += String(
            Config::CC1101_FREQ_MHZ,
            3
        );
        json += ",";

        json += "\"rssi_dbm\":";
        json += String(
            RxReceiver::getCurrentRssi()
        );
        json += ",";

        json += "\"dropped_edges\":";
        json += String(
            RxReceiver::getDroppedEdges()
        );

        json += "},";

        json += "\"circuits\":{";

        for (
            uint8_t circuit = 1;
            circuit <= 3;
            circuit++
        )
        {
            RfState::CircuitState state =
                RfState::getState(circuit);

            json += "\"";
            json += String(circuit);
            json += "\":{";

            json += "\"known\":";
            json +=
                state.known
                ? "true"
                : "false";
            json += ",";

            json += "\"state\":\"";

            if (!state.known)
            {
                json += "UNKNOWN";
            }
            else if (state.isOn)
            {
                json += "ON";
            }
            else
            {
                json += "OFF";
            }

            json += "\",";

            json += "\"source\":\"";
            json +=
                RfState::sourceToString(
                    state.source
                );
            json += "\",";

            json += "\"last_update_ms\":";
            json += String(
                state.lastUpdateMs
            );

            json += "}";

            if (circuit < 3)
            {
                json += ",";
            }
        }

        json += "},";


        json += "\"messages\":[";

        const size_t count =
            RxReceiver::getMessageCount();

        for (
            size_t i = 0;
            i < count;
            i++
        )
        {
            RxReceiver::ReceivedMessage message;

            if (
                !RxReceiver::getMessage(
                    i,
                    message
                )
            )
            {
                continue;
            }

            char codeBuffer[16];

            snprintf(
                codeBuffer,
                sizeof(codeBuffer),
                "0x%010llX",
                static_cast<unsigned long long>(
                    message.code
                )
            );

            json += "{";

            json += "\"sequence\":";
            json += String(
                message.sequence
            );
            json += ",";

            json += "\"timestamp_ms\":";
            json += String(
                static_cast<unsigned long long>(
                    message.timestampMs
                )
            );
            json += ",";

            json += "\"code\":\"";
            json += codeBuffer;
            json += "\",";

            json += "\"rssi_dbm\":";
            json += String(
                message.rssiDbm
            );

            json += "}";

            if (i + 1 < count)
            {
                json += ",";
            }
        }

        json += "]";
        json += "}";

        return json;
    }

    void handleRoot()
    {
        server.send_P(
            200,
            "text/html",
            INDEX_HTML
        );
    }
   
    void handleApiStatus()
    {
        server.send(
            200,
            "application/json",
            buildStatusJson()
        );
    }

    void handleApiState()
    {
        String json;
        json.reserve(512);

        json += "{";

        for (
            uint8_t circuit = 1;
            circuit <= 3;
            circuit++
        )
        {
            RfState::CircuitState state =
                RfState::getState(circuit);

            json += "\"";
            json += String(circuit);
            json += "\":{";

            json += "\"known\":";
            json +=
                state.known
                ? "true"
                : "false";
            json += ",";

            json += "\"state\":\"";

            if (!state.known)
            {
                json += "UNKNOWN";
            }
            else if (state.isOn)
            {
                json += "ON";
            }
            else
            {
                json += "OFF";
            }

            json += "\",";

            json += "\"source\":\"";
            json +=
                RfState::sourceToString(
                    state.source
                );
            json += "\",";

            json += "\"last_update_ms\":";
            json += String(
                state.lastUpdateMs
            );

            json += "}";

            if (circuit < 3)
            {
                json += ",";
            }
        }

        json += "}";

        server.send(
            200,
            "application/json",
            json
        );
    }

    void configureHttpServer()
    {
        server.on(
            "/",
            HTTP_GET,
            handleRoot
        );

        server.on(
            "/api/status",
            HTTP_GET,
            handleApiStatus
        );

        server.on(
            "/api/state",
            HTTP_GET,
            handleApiState
        );

        server.on(
            "/api/tx/c1/on",
            HTTP_POST,
            []()
            {
                bool success =
                    TxTransmitter::sendCircuit1On();

                if (success)
                {
                    server.send(
                        200,
                        "application/json",
                        "{\"success\":true,\"circuit\":1,\"action\":\"ON\"}"
                    );
                }
                else
                {
                    server.send(
                        500,
                        "application/json",
                        "{\"success\":false,\"circuit\":1,\"action\":\"ON\"}"
                    );
                }
            }
        );

        server.on(
            "/api/tx/c1/off",
            HTTP_POST,
            []()
            {
                bool success =
                    TxTransmitter::sendCircuit1Off();

                if (success)
                {
                    server.send(
                        200,
                        "application/json",
                        "{\"success\":true,\"circuit\":1,\"action\":\"OFF\"}"
                    );
                }
                else
                {
                    server.send(
                        500,
                        "application/json",
                        "{\"success\":false,\"circuit\":1,\"action\":\"OFF\"}"
                    );
                }
            }
        );

        server.on(
            "/api/tx/status",
            HTTP_GET,
            []()
            {
                String json = "{";

                json += "\"next_counter\":";
                json += String(
                    TxTransmitter::getNextCounter()
                );

                json += "}";

                server.send(
                    200,
                    "application/json",
                    json
                );
            }
        );

        server.onNotFound(
            []()
            {
                server.send(
                    404,
                    "text/plain",
                    "Not Found"
                );
            }
        );

        server.begin();

        Serial.println(
            "HTTP server started."
        );
    }
}

namespace WebServerApp
{
    void begin()
    {
        connectWifi();
        configureTime();
        configureMdns();
        configureHttpServer();

        printStatus();
    }

    void process()
    {
        server.handleClient();

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println(
                "Wi-Fi connection lost."
            );

            connectWifi();
        }
    }

    void printStatus()
    {
        Serial.println();
        Serial.println(
            "----- SYSTEM STATUS -----"
        );

        Serial.print("Time:       ");
        Serial.println(
            getFormattedTime()
        );

        Serial.print("NTP Sync:   ");
        Serial.println(
            isTimeSynchronized()
            ? "YES"
            : "NO"
        );

        Serial.print("Wi-Fi:      ");
        Serial.println(
            WiFi.status() == WL_CONNECTED
            ? "CONNECTED"
            : "DISCONNECTED"
        );

        Serial.print("SSID:       ");
        Serial.println(WiFi.SSID());

        Serial.print("IP:         ");
        Serial.println(WiFi.localIP());

        Serial.print("RSSI:       ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");

        Serial.print("Web:        http://");
        Serial.print(Config::HOSTNAME);
        Serial.println(".local");

        Serial.print("CC1101:     ");
        Serial.println(
            RxReceiver::isConnected()
            ? "CONNECTED"
            : "NOT CONNECTED"
        );

        if (RxReceiver::isConnected())
        {
            Serial.print("RF Freq:    ");
            Serial.print(
                Config::CC1101_FREQ_MHZ,
                3
            );
            Serial.println(" MHz");

            Serial.print("RF RSSI:    ");
            Serial.print(
                RxReceiver::getCurrentRssi()
            );
            Serial.println(" dBm");
        }

        Serial.print("Messages:   ");
        Serial.println(
            RxReceiver::getMessageCount()
        );

        Serial.print("Dropped:    ");
        Serial.println(
            RxReceiver::getDroppedEdges()
        );

        Serial.println(
            "-------------------------"
        );
    }
}
