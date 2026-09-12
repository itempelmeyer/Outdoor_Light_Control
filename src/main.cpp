#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>

#include "secrets.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

static constexpr char HOSTNAME[] = "local433signals";

// Central Time with automatic DST
static constexpr char TZ_INFO[] =
    "CST6CDT,M3.2.0/2,M11.1.0/2";

static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";

static constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000;

// -----------------------------------------------------------------------------
// Web server
// -----------------------------------------------------------------------------

WebServer server(80);

// -----------------------------------------------------------------------------
// RF signal storage
//
// For now this is transport/storage infrastructure.
// The CC1101 receiver will feed this structure next.
// -----------------------------------------------------------------------------

struct RfSignal
{
    uint32_t sequence;
    uint64_t timestampMs;
    bool level;
    uint32_t durationUs;
};

static constexpr size_t MAX_SIGNALS = 100;

RfSignal signals[MAX_SIGNALS];

size_t signalWriteIndex = 0;
size_t signalCount = 0;

uint32_t signalSequence = 0;

// -----------------------------------------------------------------------------
// Time helpers
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// RF logging
// -----------------------------------------------------------------------------

void recordRfSignal(bool level, uint32_t durationUs)
{
    RfSignal &signal = signals[signalWriteIndex];

    signal.sequence = ++signalSequence;
    signal.timestampMs = getEpochMilliseconds();
    signal.level = level;
    signal.durationUs = durationUs;

    signalWriteIndex =
        (signalWriteIndex + 1) % MAX_SIGNALS;

    if (signalCount < MAX_SIGNALS)
    {
        signalCount++;
    }

    Serial.printf(
        "[RF] %s | #%lu | %s | %lu us\n",
        getFormattedTime().c_str(),
        static_cast<unsigned long>(signal.sequence),
        signal.level ? "HIGH" : "LOW ",
        static_cast<unsigned long>(signal.durationUs)
    );
}

// -----------------------------------------------------------------------------
// Wi-Fi
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// NTP
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// mDNS
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// JSON API
// -----------------------------------------------------------------------------

String buildStatusJson()
{
    String json;

    json.reserve(12000);

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

    json += "\"signals\":[";

    for (size_t i = 0; i < signalCount; i++)
    {
        size_t index =
            (signalWriteIndex + MAX_SIGNALS - signalCount + i)
            % MAX_SIGNALS;

        const RfSignal &signal = signals[index];

        json += "{";

        json += "\"sequence\":";
        json += String(signal.sequence);
        json += ",";

        json += "\"timestamp_ms\":";
        json += String(
            static_cast<unsigned long long>(signal.timestampMs)
        );
        json += ",";

        json += "\"level\":";
        json += signal.level ? "1" : "0";
        json += ",";

        json += "\"duration_us\":";
        json += String(signal.durationUs);

        json += "}";

        if (i + 1 < signalCount)
        {
            json += ",";
        }
    }

    json += "]";

    json += "}";

    return json;
}

// -----------------------------------------------------------------------------
// Web page
// -----------------------------------------------------------------------------

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>

<meta charset="UTF-8">

<meta
    name="viewport"
    content="width=device-width, initial-scale=1.0"
>

<title>433 MHz Signal Monitor</title>

<style>

body {
    font-family: monospace;
    background: #111;
    color: #ddd;
    margin: 20px;
}

h1 {
    color: #fff;
}

.panel {
    border: 1px solid #555;
    padding: 15px;
    margin-bottom: 20px;
    border-radius: 6px;
}

.good {
    color: #5f5;
}

.warning {
    color: #ff5;
}

table {
    width: 100%;
    border-collapse: collapse;
}

th, td {
    border-bottom: 1px solid #333;
    padding: 6px;
    text-align: left;
}

th {
    color: #aaa;
}

.high {
    color: #5f5;
}

.low {
    color: #5af;
}

</style>

</head>

<body>

<h1>433 MHz Signal Monitor</h1>

<div class="panel">

    <div>
        Time:
        <span id="time">---</span>
    </div>

    <div>
        NTP:
        <span id="sync">---</span>
    </div>

    <div>
        Uptime:
        <span id="uptime">---</span>
    </div>

</div>

<div class="panel">

    <div>
        SSID:
        <span id="ssid">---</span>
    </div>

    <div>
        IP:
        <span id="ip">---</span>
    </div>

    <div>
        Wi-Fi RSSI:
        <span id="rssi">---</span>
        dBm
    </div>

</div>

<div class="panel">

<h2>Received RF Pulses</h2>

<table>

<thead>
<tr>
    <th>#</th>
    <th>Timestamp</th>
    <th>Level</th>
    <th>Duration</th>
</tr>
</thead>

<tbody id="signals">
</tbody>

</table>

</div>

<script>

function formatUptime(ms)
{
    let seconds = Math.floor(ms / 1000);

    const days = Math.floor(seconds / 86400);
    seconds %= 86400;

    const hours = Math.floor(seconds / 3600);
    seconds %= 3600;

    const minutes = Math.floor(seconds / 60);
    seconds %= 60;

    return `${days}d ${hours}h ${minutes}m ${seconds}s`;
}

async function refresh()
{
    try
    {
        const response =
            await fetch('/api/status');

        const data =
            await response.json();

        document.getElementById('time')
            .textContent = data.time;

        document.getElementById('sync')
            .textContent =
                data.time_synced
                ? 'SYNCED'
                : 'NOT SYNCED';

        document.getElementById('uptime')
            .textContent =
                formatUptime(data.uptime_ms);

        document.getElementById('ssid')
            .textContent =
                data.wifi.ssid;

        document.getElementById('ip')
            .textContent =
                data.wifi.ip;

        document.getElementById('rssi')
            .textContent =
                data.wifi.rssi_dbm;

        const tbody =
            document.getElementById('signals');

        tbody.innerHTML = '';

        const signals =
            [...data.signals].reverse();

        for (const signal of signals)
        {
            const row =
                document.createElement('tr');

            const levelText =
                signal.level
                ? 'HIGH'
                : 'LOW';

            const levelClass =
                signal.level
                ? 'high'
                : 'low';

            row.innerHTML = `
                <td>${signal.sequence}</td>
                <td>${signal.timestamp_ms}</td>
                <td class="${levelClass}">
                    ${levelText}
                </td>
                <td>${signal.duration_us} us</td>
            `;

            tbody.appendChild(row);
        }
    }
    catch (error)
    {
        console.error(error);
    }
}

setInterval(refresh, 1000);

refresh();

</script>

</body>
</html>
)rawliteral";

// -----------------------------------------------------------------------------
// HTTP handlers
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Status output
// -----------------------------------------------------------------------------

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

    Serial.print("RF Pulses:  ");
    Serial.println(signalCount);

    Serial.println("-------------------------");
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("============================");
    Serial.println("  433 MHz Signal Receiver");
    Serial.println("============================");
    Serial.println();

    connectWifi();

    configureTime();

    configureMdns();

    configureWebServer();

    printStatus();

    // -------------------------------------------------------------------------
    // Temporary test data
    //
    // DELETE these once the CC1101 is connected.
    //
    // These emulate the ~300 / ~900 us pulses we observed from your remote.
    // -------------------------------------------------------------------------

    recordRfSignal(true, 326);
    recordRfSignal(false, 851);

    recordRfSignal(true, 918);
    recordRfSignal(false, 263);
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

void loop()
{
    server.handleClient();

    static uint32_t lastStatusPrint = 0;

    if (
        millis() - lastStatusPrint
        >= STATUS_PRINT_INTERVAL_MS
    )
    {
        lastStatusPrint = millis();

        printStatus();
    }

    // Reconnect Wi-Fi if it drops.
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Wi-Fi connection lost.");
        connectWifi();
    }
}