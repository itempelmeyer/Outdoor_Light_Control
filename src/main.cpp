#include <Arduino.h>

#include "rx_receiver.h"
#include "web_server.h"

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
    WebServerApp::begin();
}

void loop()
{
    WebServerApp::process();
    RxReceiver::process();
}
