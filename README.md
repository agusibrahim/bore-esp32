# bore-esp32

An ESP32 port of [bore](https://github.com/ekzhang/bore) — a lightweight TCP tunnel that punches through NAT so devices on your local network can receive inbound connections from the internet.

The original `bore` is written in Rust (~400 lines) and runs on desktops and servers. This project reimplements just the **client side** in C++ so it runs directly on an ESP32-C3 with FreeRTOS, no extra hardware needed.

## What it does

You have a web server (or any TCP service) running on your ESP32. Normally it's stuck behind NAT — only reachable from your LAN. This library connects to a `bore` server (like the public one at `bore.pub`) and opens a tunnel. Anyone on the internet can then hit `bore.pub:<port>` and the traffic gets proxied straight to your ESP32.

No port forwarding, no DDNS, no cloud relay. Just a direct TCP tunnel.

## Quick start

### Wiring it up

Copy the `lib/bore/` folder into your PlatformIO project's `lib/` directory and add the dependency:

```ini
[env:esp32-c3]
platform = espressif32
board = esp32-c3-devkitc-02
framework = arduino

build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

lib_deps =
    bblanchon/ArduinoJson@^7.0.0
```

The `ARDUINO_USB_CDC_ON_BOOT` flag is needed on most ESP32-C3 boards to get serial output over USB.

### Minimal example

```cpp
#include <WiFi.h>
#include <WebServer.h>
#include <BoreClient.h>

WebServer server(80);
BoreClient bore;

void setup() {
    Serial.begin(115200);

    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    server.on("/", []() {
        server.send(200, "text/html", "<h1>Hello from ESP32</h1>");
    });
    server.begin();

    if (bore.begin("bore.pub", 80)) {
        Serial.printf("Live at http://bore.pub:%d\n", bore.remotePort());
    }
}

void loop() {
    bore.loop();
    server.handleClient();
}
```

The `begin()` call does the full handshake and blocks until it gets a public port (or fails after ~3 seconds). After that, call `loop()` on every iteration so incoming connections get dispatched.

## API

| Method | Description |
|---|---|
| `begin(host, localPort, requestedPort)` | Connect to bore server. `requestedPort` defaults to 0 (random). Returns `true` on success. |
| `loop()` | Must be called repeatedly. Processes control messages and spawns proxy tasks for new connections. |
| `remotePort()` | Returns the public port the server assigned. 0 if not connected. |
| `isConnected()` | Whether the control channel is alive. |
| `stop()` | Tears down everything — control connection and all active proxies. |

## Under the hood

Each incoming TCP connection spawns a FreeRTOS task that:
1. Opens a new connection to the bore server
2. Sends the Accept message with the connection UUID
3. Waits for data from the server (the visitor's HTTP request or whatever)
4. Connects to `127.0.0.1` on your local port
5. Shuttles bytes back and forth until either side disconnects

Up to 3 concurrent connections run simultaneously by default. You can bump `BORE_MAX_PROXY_CONNECTIONS` in the header if you need more, but each one costs ~2KB of stack plus the TCP buffers.

Memory usage on a ESP32-C3 (320KB SRAM):
- Static allocation: ~5KB (JSON buffers, proxy tracking)
- Per proxy connection: ~1KB stack + 1KB TCP buffers
- Total with 3 active connections: ~10KB

## Included example

`examples/simple_web_server/` has a ready-to-flash project with:
- WiFi connection
- A styled web page served from the ESP32
- A toggle switch that controls the built-in LED (GPIO 8)
- Live heap and uptime display
- Bore tunnel for public access

```bash
cd examples/simple_web_server
pio run --target upload
pio device monitor
```

You'll see something like:

```
WiFi OK! IP: 192.168.1.42
Bore OK! http://bore.pub:44521
=== Ready ===
```

Open that URL from any device — phone on cellular, friend in another city — and you're hitting the ESP32 directly.

## Running your own server

The public `bore.pub` instance works fine for testing, but for anything long-running you probably want your own. It's one command:

```bash
# on a VPS or any public machine
bore server
```

Then point the library at your server:

```cpp
bore.begin("your-server.com", 80);
```

The `bore` binary is available from the [original repo](https://github.com/ekzhang/bore) for macOS, Linux, and Windows, or via `cargo install bore-cli`.

## Limitations

- **No TLS.** The bore protocol is plaintext TCP. If you need encryption, terminate TLS on the ESP32 or run bore behind a reverse proxy.
- **No auth.** The HMAC challenge from the original protocol was omitted for code size. Use a private bore server if access control matters.
- **3 concurrent connections.** Each proxy hogs a FreeRTOS task and TCP socket. The ESP32-C3 handles more, but RAM becomes the bottleneck.
- **Browser HTTPS attempts.** Modern browsers try HTTPS first before falling back to HTTP. You'll see a harmless "Invalid request" in the log from the rejected TLS handshake — the actual HTTP request works fine after.

## License

MIT. The original bore project is by [Eric Zhang](https://github.com/ekzhang) and also MIT licensed.
