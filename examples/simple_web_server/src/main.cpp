#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BoreClient.h>

// WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Built-in LED on ESP32-C3 (GPIO 8)
#define BUILTIN_LED 8

// Web server on port 80
WebServer server(80);

// Bore client
BoreClient bore;

// LED state
bool ledState = false;

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-C3 Bore Tunnel</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            min-height: 100vh;
            background: linear-gradient(135deg, #0f0c29 0%, #302b63 50%, #24243e 100%);
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .card {
            background: rgba(255,255,255,0.95);
            border-radius: 20px;
            padding: 40px;
            box-shadow: 0 25px 60px rgba(0,0,0,0.3);
            max-width: 420px;
            width: 100%;
        }
        h1 {
            color: #1a1a2e;
            font-size: 24px;
            margin-bottom: 6px;
        }
        .subtitle {
            color: #10b981;
            font-weight: 600;
            font-size: 14px;
            margin-bottom: 24px;
        }
        .info-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-bottom: 28px;
        }
        .info-box {
            background: #f3f4f6;
            padding: 12px 14px;
            border-radius: 10px;
        }
        .info-box .label {
            font-size: 11px;
            color: #9ca3af;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .info-box .value {
            font-size: 15px;
            font-weight: 600;
            color: #1f2937;
            margin-top: 2px;
        }
        .led-section {
            background: #f9fafb;
            border-radius: 14px;
            padding: 24px;
            text-align: center;
            margin-bottom: 20px;
        }
        .led-label {
            font-size: 13px;
            color: #6b7280;
            margin-bottom: 16px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        .switch-container {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 20px;
        }
        .switch {
            position: relative;
            width: 80px;
            height: 44px;
        }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider {
            position: absolute;
            cursor: pointer;
            inset: 0;
            background: #d1d5db;
            border-radius: 44px;
            transition: all 0.4s cubic-bezier(0.68, -0.55, 0.265, 1.55);
        }
        .slider:before {
            content: '';
            position: absolute;
            height: 36px;
            width: 36px;
            left: 4px;
            bottom: 4px;
            background: white;
            border-radius: 50%;
            transition: all 0.4s cubic-bezier(0.68, -0.55, 0.265, 1.55);
            box-shadow: 0 2px 8px rgba(0,0,0,0.15);
        }
        input:checked + .slider {
            background: linear-gradient(135deg, #10b981, #059669);
            box-shadow: 0 0 20px rgba(16, 185, 129, 0.4);
        }
        input:checked + .slider:before {
            transform: translateX(36px);
        }
        .led-status {
            font-size: 14px;
            font-weight: 600;
            min-width: 60px;
        }
        .led-status.on { color: #10b981; }
        .led-status.off { color: #9ca3af; }
        .led-indicator {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            display: inline-block;
            margin-right: 6px;
            vertical-align: middle;
            transition: all 0.3s;
        }
        .led-indicator.on {
            background: #10b981;
            box-shadow: 0 0 12px rgba(16, 185, 129, 0.6);
        }
        .led-indicator.off {
            background: #d1d5db;
        }
        .chips {
            display: flex;
            flex-wrap: wrap;
            gap: 6px;
        }
        .chip {
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 11px;
            font-weight: 500;
        }
        .footer {
            margin-top: 20px;
            text-align: center;
            color: #9ca3af;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>ESP32-C3 + Bore Tunnel</h1>
        <p class="subtitle">Connected via Bore TCP Tunnel</p>

        <div class="info-grid">
            <div class="info-box">
                <div class="label">Local Port</div>
                <div class="value">80</div>
            </div>
            <div class="info-box">
                <div class="label">Public Port</div>
                <div class="value" id="publicPort">-</div>
            </div>
            <div class="info-box">
                <div class="label">Uptime</div>
                <div class="value" id="uptime">0s</div>
            </div>
            <div class="info-box">
                <div class="label">Free Heap</div>
                <div class="value" id="heap">-</div>
            </div>
        </div>

        <div class="led-section">
            <div class="led-label">Built-in LED (GPIO 8)</div>
            <div class="switch-container">
                <span class="led-status off" id="statusText">
                    <span class="led-indicator off" id="indicator"></span>OFF
                </span>
                <label class="switch">
                    <input type="checkbox" id="ledToggle" onchange="toggleLED()">
                    <span class="slider"></span>
                </label>
            </div>
        </div>

        <div class="chips">
            <span class="chip">ESP32-C3</span>
            <span class="chip">FreeRTOS</span>
            <span class="chip">Bore Protocol</span>
            <span class="chip">WiFi</span>
        </div>

        <p class="footer">Served from ESP32-C3 via Bore tunnel</p>
    </div>

    <script>
        const port = __PUBLIC_PORT__;
        document.getElementById('publicPort').textContent = port;

        function toggleLED() {
            const btn = document.getElementById('ledToggle');
            fetch('/led?state=' + (btn.checked ? '1' : '0'))
                .then(r => r.json())
                .then(d => updateUI(d.state))
                .catch(() => updateUI(btn.checked));
        }

        function updateUI(on) {
            const btn = document.getElementById('ledToggle');
            const status = document.getElementById('statusText');
            const ind = document.getElementById('indicator');
            btn.checked = on;
            status.className = 'led-status ' + (on ? 'on' : 'off');
            status.innerHTML = '<span class="led-indicator ' + (on ? 'on' : 'off') + '" id="indicator"></span>' + (on ? 'ON' : 'OFF');
        }

        function getHeap() {
            fetch('/heap').then(r => r.text()).then(v => {
                document.getElementById('heap').textContent = v;
            }).catch(() => {});
        }

        setInterval(() => {
            const s = Math.floor((Date.now() - start) / 1000);
            const m = Math.floor(s / 60);
            document.getElementById('uptime').textContent = m > 0 ? m + 'm ' + (s % 60) + 's' : s + 's';
        }, 1000);
        setInterval(getHeap, 5000);
        getHeap();

        const start = Date.now();
    </script>
</body>
</html>
)rawliteral";

    html.replace("__PUBLIC_PORT__", String(bore.remotePort()));
    server.send(200, "text/html", html);
}

void handleLed() {
    if (server.hasArg("state")) {
        ledState = server.arg("state") == "1";
        digitalWrite(BUILTIN_LED, ledState ? HIGH : LOW);
    }
    String json = "{\"state\":" + String(ledState ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

void handleHeap() {
    server.send(200, "text/plain", String(ESP.getFreeHeap()));
}

void handleNotFound() {
    server.send(404, "text/plain", "404");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Setup LED
    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);

    Serial.println("\n=== ESP32-C3 Bore Tunnel ===\n");

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi failed!");
        return;
    }
    Serial.printf("\nWiFi OK! IP: %s\n", WiFi.localIP().toString().c_str());

    // Setup web server
    server.on("/", handleRoot);
    server.on("/led", handleLed);
    server.on("/heap", handleHeap);
    server.onNotFound(handleNotFound);
    server.begin();

    // Connect to Bore
    if (bore.begin("bore.pub", 80)) {
        Serial.printf("Bore OK! http://bore.pub:%d\n", bore.remotePort());
    } else {
        Serial.println("Bore failed!");
    }

    Serial.println("=== Ready ===\n");
}

void loop() {
    bore.loop();
    server.handleClient();
    delay(1);
}
