#include <Arduino.h>

#include "rx_receiver.h"
#include "web_server.h"
#include "tx_transmitter.h"

void handleSerialDiagnostics()
{
    if (!Serial.available())
    {
        return;
    }

    char command = Serial.read();

    switch (command)
    {
        case '1':
            TxTransmitter::sendCircuit1On();
            break;

        case '0':
            TxTransmitter::sendCircuit1Off();
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println(
        "============================"
    );
    Serial.println(
        "  433 MHz Signal Receiver"
    );
    Serial.println(
        "============================"
    );
    Serial.println();

    RxReceiver::begin();
    TxTransmitter::begin();
    WebServerApp::begin();
}

void loop()
{
    WebServerApp::process();
    RxReceiver::process();
    handleSerialDiagnostics();

}
