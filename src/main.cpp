#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "secrets.h"
#include "web_page.h"

#define CC1101_GDO0  36
#define CC1101_CS    17
#define CC1101_SCK   22
#define CC1101_MOSI  23
#define CC1101_MISO  32

static constexpr float CC1101_FREQ_MHZ = 433.945;
static constexpr uint32_t SHORT_MIN_US = 200;
static constexpr uint32_t SHORT_MAX_US = 450;
static constexpr uint32_t LONG_MIN_US = 700;
static constexpr uint32_t LONG_MAX_US = 1050;
static constexpr uint32_t FRAME_GAP_US = 3000;
static constexpr size_t EDGE_BUFFER_SIZE = 512;
static constexpr char HOSTNAME[] = "local433signals";
static constexpr char TZ_INFO[] = "CST6CDT,M3.2.0/2,M11.1.0/2";
static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";
static constexpr size_t MAX_MESSAGES = 50;

uint64_t lastPrintedRfCode = 0;
uint32_t lastPrintedRfMs = 0;

static constexpr uint32_t RF_DUPLICATE_WINDOW_MS = 500;

struct RawEdge
{
    uint32_t durationUs;
    bool level;
};

volatile RawEdge edgeBuffer[EDGE_BUFFER_SIZE];
volatile size_t edgeWriteIndex = 0;
volatile size_t edgeReadIndex = 0;
volatile uint32_t lastEdgeMicros = 0;
volatile uint32_t droppedEdges = 0;

struct FramePulse
{
    bool level;
    uint32_t durationUs;
};

static constexpr size_t MAX_FRAME_PULSES = 128;

FramePulse framePulses[MAX_FRAME_PULSES];

size_t framePulseCount = 0;

int frameRssiPeak = -999;

bool isShortPulse(uint32_t durationUs)
{
    return
        durationUs >= SHORT_MIN_US &&
        durationUs <= SHORT_MAX_US;
}

bool isLongPulse(uint32_t durationUs)
{
    return
        durationUs >= LONG_MIN_US &&
        durationUs <= LONG_MAX_US;
}

bool cc1101Connected = false;
int cc1101Rssi = -999;


void IRAM_ATTR handleRfEdge();
void processRfEdges();
void finishFrame();


WebServer server(80);

struct ReceivedMessage
{
    uint32_t sequence;
    uint64_t timestampMs;
    uint64_t code;
    int rssiDbm;
};

ReceivedMessage messages[MAX_MESSAGES];

size_t messageWriteIndex = 0;
size_t messageCount = 0;
uint32_t messageSequence = 0;

bool isTimeSynchronized()
{
    time_t now = time(nullptr);

    // Anything comfortably after Jan 1 2024 means NTP has likely succeeded.
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

uint64_t getEpochMilliseconds()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    return
        static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
        static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void recordReceivedMessage(uint64_t code, int rssiDbm)
{
    ReceivedMessage &message =
        messages[messageWriteIndex];

    message.sequence = ++messageSequence;
    message.timestampMs = getEpochMilliseconds();
    message.code = code;
    message.rssiDbm = rssiDbm;

    messageWriteIndex =
        (messageWriteIndex + 1) % MAX_MESSAGES;

    if (messageCount < MAX_MESSAGES)
    {
        messageCount++;
    }
}

void connectWifi()
{
    Serial.println();
    Serial.println("Connecting to Wi-Fi...");
    Serial.printf("SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);

    WiFi.setHostname(HOSTNAME);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    uint32_t startTime = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - startTime > 30000)
        {
            Serial.println();
            Serial.println("Wi-Fi connection timeout.");
            ESP.restart();
        }
    }

    Serial.println();
    Serial.println("Wi-Fi connected.");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
}

void configureTime()
{
    Serial.println("Starting NTP synchronization...");

    configTzTime(
        TZ_INFO,
        NTP_SERVER_1,
        NTP_SERVER_2
    );

    uint32_t startTime = millis();

    while (!isTimeSynchronized())
    {
        delay(250);
        Serial.print(".");

        if (millis() - startTime > 15000)
        {
            Serial.println();
            Serial.println("NTP synchronization timed out.");
            return;
        }
    }

    Serial.println();
    Serial.print("Time synchronized: ");
    Serial.println(getFormattedTime());
}

void configureMdns()
{
    if (!MDNS.begin(HOSTNAME))
    {
        Serial.println("ERROR: Failed to start mDNS.");
        return;
    }

    MDNS.addService(
        "http",
        "tcp",
        80
    );

    Serial.printf(
        "mDNS started: http://%s.local\n",
        HOSTNAME
    );
}

String buildStatusJson()
{
    String json;

    json.reserve(8000);

    json += "{";

    json += "\"hostname\":\"";
    json += HOSTNAME;
    json += "\",";

    json += "\"time\":\"";
    json += getFormattedTime();
    json += "\",";

    json += "\"time_synced\":";
    json += isTimeSynchronized() ? "true" : "false";
    json += ",";

    json += "\"uptime_ms\":";
    json += String(millis());
    json += ",";

    json += "\"wifi\":{";

    json += "\"ssid\":\"";
    json += WiFi.SSID();
    json += "\",";

    json += "\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\",";

    json += "\"rssi_dbm\":";
    json += String(WiFi.RSSI());

    json += "},";

    json += "\"rf\":{";

    json += "\"connected\":";
    json += cc1101Connected ? "true" : "false";
    json += ",";

    json += "\"frequency_mhz\":";
    json += String(CC1101_FREQ_MHZ, 3);
    json += ",";

    json += "\"rssi_dbm\":";
    json += String(cc1101Rssi);
    json += ",";

    json += "\"dropped_edges\":";
    json += String(
        static_cast<unsigned long>(droppedEdges)
    );

    json += "},";

    json += "\"messages\":[";

    for (size_t i = 0; i < messageCount; i++)
    {
        size_t index =
            (
                messageWriteIndex +
                MAX_MESSAGES -
                messageCount +
                i
            ) % MAX_MESSAGES;

        const ReceivedMessage &message =
            messages[index];

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
        json += String(message.sequence);
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
        json += String(message.rssiDbm);

        json += "}";

        if (i + 1 < messageCount)
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

void configureWebServer()
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

    Serial.println("HTTP server started.");
}

void printStatus()
{
    Serial.println();
    Serial.println("----- SYSTEM STATUS -----");

    Serial.print("Time:       ");
    Serial.println(getFormattedTime());

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
    Serial.print(HOSTNAME);
    Serial.println(".local");

    Serial.print("CC1101:     ");
    Serial.println(
        cc1101Connected
        ? "CONNECTED"
        : "NOT CONNECTED"
    );

    if (cc1101Connected)
    {
        cc1101Rssi = ELECHOUSE_cc1101.getRssi();

        Serial.print("RF Freq:    ");
        Serial.print(CC1101_FREQ_MHZ, 3);
        Serial.println(" MHz");

        Serial.print("RF Floor RSSI:    ");
        Serial.print(cc1101Rssi);
        Serial.println(" dBm");

}

    Serial.print("Messages:   ");
    Serial.println(messageCount);

    Serial.print("Dropped:    ");
    Serial.println(droppedEdges);

    Serial.println("-------------------------");
}

void configureCC1101()
{
    Serial.println();
    Serial.println("----- CC1101 INIT -----");

    ELECHOUSE_cc1101.setSpiPin(
        CC1101_SCK,
        CC1101_MISO,
        CC1101_MOSI,
        CC1101_CS
    );

    ELECHOUSE_cc1101.setGDO0(CC1101_GDO0);

    Serial.printf(
        "SPI: SCK=%d MISO=%d MOSI=%d CS=%d\n",
        CC1101_SCK,
        CC1101_MISO,
        CC1101_MOSI,
        CC1101_CS
    );

    Serial.printf(
        "GDO0: GPIO%d\n",
        CC1101_GDO0
    );

    // Initialize chip first.
    ELECHOUSE_cc1101.Init();

    // Verify communication using hardware status registers.
    byte partnum =
        ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM);

    byte version =
        ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION);

    Serial.printf(
        "PARTNUM: 0x%02X\n",
        partnum
    );

    Serial.printf(
        "VERSION: 0x%02X\n",
        version
    );

    // Valid SPI communication:
    // PARTNUM normally = 0x00.
    // VERSION may vary by CC1101 silicon/module.
    cc1101Connected =
        (partnum == 0x00) &&
        (version != 0x00) &&
        (version != 0xFF);

    if (!cc1101Connected)
    {
        Serial.println("CC1101: CONNECTION FAILED");
        Serial.println("-----------------------");
        return;
    }

    Serial.println("CC1101: CONNECTION OK");

    // ASK/OOK
    ELECHOUSE_cc1101.setModulation(2);

    // Center frequency observed from original remote
    ELECHOUSE_cc1101.setMHZ(CC1101_FREQ_MHZ);

    // Raw asynchronous serial mode
    ELECHOUSE_cc1101.setPktFormat(3);

    // RX channel bandwidth
    ELECHOUSE_cc1101.setRxBW(203.125);

    // Enter RX
    ELECHOUSE_cc1101.SetRx();

    pinMode(CC1101_GDO0, INPUT);

    lastEdgeMicros = micros();

    attachInterrupt(
        digitalPinToInterrupt(CC1101_GDO0),
        handleRfEdge,
        CHANGE
    );

    Serial.println("GDO0 edge capture: ENABLED");

    delay(10);

    cc1101Rssi = ELECHOUSE_cc1101.getRssi();

    Serial.printf(
        "Frequency: %.3f MHz\n",
        CC1101_FREQ_MHZ
    );

    Serial.println("Modulation: ASK/OOK");
    Serial.println("Packet mode: ASYNC RAW");

    Serial.printf(
        "Initial RF RSSI: %d dBm\n",
        cc1101Rssi
    );

    Serial.println("Mode: RX");
    Serial.println("-----------------------");
}

void IRAM_ATTR handleRfEdge()
{
    uint32_t now = micros();

    uint32_t duration =
        now - lastEdgeMicros;

    lastEdgeMicros = now;

    bool level =
        digitalRead(CC1101_GDO0);

    size_t next =
        (edgeWriteIndex + 1) % EDGE_BUFFER_SIZE;

    // Buffer full: drop this edge.
    if (next == edgeReadIndex)
    {
        droppedEdges++;
        return;
    }

    edgeBuffer[edgeWriteIndex].durationUs =
        duration;

    // The duration that just ended belongs to the
    // opposite state of the current pin level.
    edgeBuffer[edgeWriteIndex].level =
        !level;

    edgeWriteIndex = next;
}

void processRfEdges()
{
    // Sample RSSI for diagnostic information.
    cc1101Rssi =
        ELECHOUSE_cc1101.getRssi();

    // Track strongest RSSI seen while collecting this frame.
    if (cc1101Rssi > frameRssiPeak)
    {
        frameRssiPeak = cc1101Rssi;
    }

    // We can process aggressively now because there is
    // no Serial printing for individual edges.
    static constexpr size_t MAX_EDGES_PER_PASS = 128;

    size_t processed = 0;

    while (
        edgeReadIndex != edgeWriteIndex &&
        processed < MAX_EDGES_PER_PASS
    )
    {
        RawEdge edge;

        noInterrupts();

        edge.durationUs =
            edgeBuffer[edgeReadIndex].durationUs;

        edge.level =
            edgeBuffer[edgeReadIndex].level;

        edgeReadIndex =
            (edgeReadIndex + 1)
            % EDGE_BUFFER_SIZE;

        interrupts();

        processed++;

        // Ignore tiny noise/glitch pulses.
        if (edge.durationUs < SHORT_MIN_US)
        {
            continue;
        }

        // A long LOW is our frame separator.
        //
        // The remote has shown roughly a 4.4 ms LOW
        // between repeated frames, so 3 ms gives us
        // comfortable separation.
        if (
            !edge.level &&
            edge.durationUs >= FRAME_GAP_US
        )
        {
            if (framePulseCount > 0)
            {
                finishFrame();
            }

            continue;
        }

        // Only retain pulses that match one of our
        // two expected timing families.
        bool validPulse =
            isShortPulse(edge.durationUs) ||
            isLongPulse(edge.durationUs);

        if (!validPulse)
        {
            continue;
        }

        // Store valid pulse.
        if (framePulseCount < MAX_FRAME_PULSES)
        {
            framePulses[framePulseCount].level =
                edge.level;

            framePulses[framePulseCount].durationUs =
                edge.durationUs;

            framePulseCount++;
        }
        else
        {
            // Corrupt/oversized frame. Start fresh.
            framePulseCount = 0;
            frameRssiPeak = -999;
        }
    }
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

    // Align decoder to the first HIGH pulse.
    while (
        startIndex < framePulseCount &&
        !framePulses[startIndex].level
    )
    {
        startIndex++;
    }

    // Decode HIGH/LOW pulse pairs directly into the numeric code.
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

        // Every symbol must be HIGH followed by LOW.
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

    // Save diagnostic values before clearing the frame.
    const int rssi = frameRssiPeak;

    // Always reset capture state.
    framePulseCount = 0;
    frameRssiPeak = -999;

    // Ignore incomplete or corrupt frames.
    if (
        bitCount != 40 ||
        invalidCount != 0
    )
    {
        return;
    }

    const uint32_t now = millis();

    // A single physical button press sends the same
    // 40-bit message repeatedly. Print it only once.
    if (
        code == lastPrintedRfCode &&
        (now - lastPrintedRfMs) < RF_DUPLICATE_WINDOW_MS
    )
    {
        return;
    }

    lastPrintedRfCode = code;
    lastPrintedRfMs = now;

    recordReceivedMessage(
        code,
        rssi
    );

    Serial.printf(
        "%s  RX  0x%010llX  RSSI=%d dBm\n",
        getFormattedTime().c_str(),
        static_cast<unsigned long long>(code),
        rssi
    );
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("============================");
    Serial.println("  433 MHz Signal Receiver");
    Serial.println("============================");
    Serial.println();

    configureCC1101();


    connectWifi();

    configureTime();

    configureMdns();

    configureWebServer();

    printStatus();
}

void loop()
{
    server.handleClient();

    processRfEdges();

    // Reconnect Wi-Fi if it drops.
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Wi-Fi connection lost.");
        connectWifi();
    }
}