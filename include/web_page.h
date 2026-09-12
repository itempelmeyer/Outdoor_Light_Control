#pragma once

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

h1, h2 {
    color: #fff;
}

.panel {
    border: 1px solid #555;
    padding: 15px;
    margin-bottom: 20px;
    border-radius: 6px;
}

.status-grid {
    display: grid;
    grid-template-columns:
        repeat(auto-fit, minmax(220px, 1fr));
    gap: 8px 24px;
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
    padding: 7px;
    text-align: left;
}

th {
    color: #aaa;
}

.code {
    color: #7df;
    font-weight: bold;
}

</style>

</head>

<body>

<h1>433 MHz Signal Monitor</h1>

<div class="panel status-grid">

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
        <span id="wifi-rssi">---</span>
        dBm
    </div>

    <div>
        CC1101:
        <span id="rf-connected">---</span>
    </div>

    <div>
        RF:
        <span id="rf-frequency">---</span>
        MHz
    </div>

    <div>
        RF RSSI:
        <span id="rf-rssi">---</span>
        dBm
    </div>

    <div>
        Dropped edges:
        <span id="dropped">---</span>
    </div>

</div>

<div class="panel">

<h2>Received Messages</h2>

<table>

<thead>
<tr>
    <th>#</th>
    <th>Timestamp</th>
    <th>Message</th>
    <th>RSSI</th>
</tr>
</thead>

<tbody id="messages">
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

function formatTimestamp(epochMs)
{
    const date = new Date(Number(epochMs));

    return date.toLocaleString();
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

        document.getElementById('wifi-rssi')
            .textContent =
                data.wifi.rssi_dbm;

        document.getElementById('rf-connected')
            .textContent =
                data.rf.connected
                ? 'CONNECTED'
                : 'NOT CONNECTED';

        document.getElementById('rf-frequency')
            .textContent =
                data.rf.frequency_mhz;

        document.getElementById('rf-rssi')
            .textContent =
                data.rf.rssi_dbm;

        document.getElementById('dropped')
            .textContent =
                data.rf.dropped_edges;

        const tbody =
            document.getElementById('messages');

        tbody.innerHTML = '';

        const messages =
            [...data.messages].reverse();

        for (const message of messages)
        {
            const row =
                document.createElement('tr');

            row.innerHTML = `
                <td>${message.sequence}</td>
                <td>
                    ${formatTimestamp(
                        message.timestamp_ms
                    )}
                </td>
                <td class="code">
                    ${message.code}
                </td>
                <td>
                    ${message.rssi_dbm} dBm
                </td>
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