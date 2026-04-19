#include "BoreClient.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>

BoreClient::BoreClient()
    : _localPort(0)
    , _remotePort(0)
    , _connected(false)
    , _recvPos(0)
{
    memset(_recvBuf, 0, BORE_RECV_BUFFER_SIZE);
    for (int i = 0; i < BORE_MAX_PROXY_CONNECTIONS; i++) {
        _proxies[i].task = nullptr;
        _proxies[i].active = false;
    }
}

BoreClient::~BoreClient() {
    stop();
}

bool BoreClient::begin(const char* remoteHost, uint16_t localPort, uint16_t requestedPort) {
    _remoteHost = remoteHost;
    _localPort = localPort;
    _remotePort = 0;
    _connected = false;
    _recvPos = 0;

    if (!connectToServer()) {
        return false;
    }

    if (!sendHello(requestedPort)) {
        _control.stop();
        return false;
    }

    if (!recvHello()) {
        _control.stop();
        return false;
    }

    _connected = true;
    return true;
}

void BoreClient::stop() {
    _connected = false;
    _control.stop();

    for (int i = 0; i < BORE_MAX_PROXY_CONNECTIONS; i++) {
        if (_proxies[i].active && _proxies[i].task != nullptr) {
            vTaskDelete(_proxies[i].task);
            _proxies[i].task = nullptr;
            _proxies[i].active = false;
        }
    }
}

void BoreClient::loop() {
    if (!_connected) {
        return;
    }

    if (!_control.connected()) {
        _connected = false;
        return;
    }

    while (_control.available() > 0) {
        String msg = recvMessage();
        if (!msg.isEmpty()) {
            processMessage(msg);
        } else {
            break;
        }
    }
}

bool BoreClient::connectToServer() {
    if (_control.connect(_remoteHost.c_str(), BORE_CONTROL_PORT)) {
        _control.setTimeout(BORE_NETWORK_TIMEOUT);
        return true;
    }
    Serial.printf("Bore: Failed to connect to %s:%d\n", _remoteHost.c_str(), BORE_CONTROL_PORT);
    return false;
}

bool BoreClient::sendHello(uint16_t port) {
    Serial.printf("Bore: Requesting port %d\n", port);

    JsonDocument doc;
    doc["Hello"] = port;

    String json;
    serializeJson(doc, json);

    bool sent = sendMessage(json);

    // Give server time to respond
    delay(100);

    return sent;
}

bool BoreClient::recvHello() {
    // Wait for data to arrive
    int waitCount = 0;
    while (_control.available() == 0 && waitCount < 30) {
        delay(100);
        waitCount++;
    }

    String msg = recvMessage();
    if (msg.isEmpty()) {
        Serial.println("Bore: No response from server");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
        Serial.printf("Bore: JSON error\n");
        return false;
    }

    if (doc["Hello"].is<uint16_t>()) {
        _remotePort = doc["Hello"].as<uint16_t>();
        Serial.printf("Bore: Public port: %d\n", _remotePort);
        return _remotePort > 0;
    }

    if (doc["Error"].is<const char*>()) {
        const char* errMsg = doc["Error"];
        Serial.printf("Bore error: %s\n", errMsg);
    }

    return false;
}

String BoreClient::recvMessage() {
    int bytesRead = 0;

    while (_control.available() > 0 && _recvPos < BORE_RECV_BUFFER_SIZE - 1) {
        char c = _control.read();
        bytesRead++;

        if (c == '\0') {
            _recvBuf[_recvPos] = '\0';
            String result(_recvBuf);
            _recvPos = 0;
            return result;
        }

        _recvBuf[_recvPos++] = c;
    }

    if (_recvPos >= BORE_RECV_BUFFER_SIZE - 1) {
        Serial.println("Bore: Buffer overflow, resetting");
        _recvPos = 0;
    }

    return "";
}

bool BoreClient::sendMessage(const String& json) {
    size_t jsonLen = json.length();
    _control.write(json.c_str(), jsonLen);
    _control.write('\0');
    _control.flush();
    return _control.connected();
}

void BoreClient::processMessage(const String& msg) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
        return;
    }

    if (doc["Heartbeat"].isNull()) {
        // Heartbeat message - do nothing
    }

    if (doc["Connection"].is<const char*>()) {
        String uuid = doc["Connection"].as<String>();
        handleConnection(uuid);
    }

    if (doc["Error"].is<const char*>()) {
        const char* errMsg = doc["Error"];
        Serial.printf("Bore error: %s\n", errMsg);
        _connected = false;
    }
}

void BoreClient::handleConnection(const String& uuid) {
    int slot = findFreeProxySlot();
    if (slot < 0) {
        Serial.println("Bore: max proxy connections reached");
        return;
    }

    ProxyTaskParams* params = new ProxyTaskParams{
        this,
        _remoteHost,
        uuid,
        _localPort,
        slot
    };

    xTaskCreate(
        proxyTask,
        "bore_proxy",
        8192,
        params,
        2,
        &_proxies[slot].task
    );

    _proxies[slot].active = true;
}

int BoreClient::findFreeProxySlot() {
    for (int i = 0; i < BORE_MAX_PROXY_CONNECTIONS; i++) {
        if (!_proxies[i].active) {
            return i;
        }
    }
    return -1;
}

void BoreClient::markProxySlotFree(int slot) {
    if (slot >= 0 && slot < BORE_MAX_PROXY_CONNECTIONS) {
        _proxies[slot].active = false;
        _proxies[slot].task = nullptr;
    }
}

void BoreClient::proxyTask(void* param) {
    ProxyTaskParams* params = static_cast<ProxyTaskParams*>(param);
    Serial.printf("Bore: New connection, slot %d\n", params->slot);

    WiFiClient remote;
    WiFiClient local;

    // Step 1: Connect to bore server and send Accept
    if (!remote.connect(params->remoteHost.c_str(), BORE_CONTROL_PORT)) {
        Serial.println("Bore proxy: failed to connect remote");
        delete params;
        vTaskDelete(NULL);
        return;
    }

    remote.setTimeout(BORE_NETWORK_TIMEOUT);

    JsonDocument doc;
    doc["Accept"] = params->uuid;

    String json;
    serializeJson(doc, json);

    remote.write(json.c_str(), json.length());
    remote.write('\0');
    remote.flush();

    // Step 2: Wait for first data from bore (the actual HTTP request)
    // This ensures we have data ready before connecting to local server
    uint8_t firstBuf[BORE_PROXY_BUFFER_SIZE];
    size_t firstLen = 0;

    int waitCount = 0;
    while (remote.available() == 0 && waitCount < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
        if (!remote.connected()) {
            Serial.println("Bore proxy: remote disconnected waiting for data");
            remote.stop();
            delete params;
            vTaskDelete(NULL);
            return;
        }
    }

    if (remote.available() > 0) {
        firstLen = remote.read(firstBuf, BORE_PROXY_BUFFER_SIZE);
        Serial.printf("Bore: Got first data (%d bytes)\n", firstLen);
    }

    // Step 3: Now connect to local server with data ready
    if (!local.connect("127.0.0.1", params->localPort)) {
        Serial.println("Bore proxy: failed to connect local");
        remote.stop();
        delete params;
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("Bore: Proxy connected, slot %d\n", params->slot);

    // Step 4: Immediately send the first data we already have
    if (firstLen > 0) {
        local.write(firstBuf, firstLen);
        local.flush();
    }

    local.setTimeout(10);
    remote.setTimeout(10);

    uint8_t bufRemote[BORE_PROXY_BUFFER_SIZE];
    uint8_t bufLocal[BORE_PROXY_BUFFER_SIZE];
    size_t totalUp = firstLen;   // bytes: remote -> local (request)
    size_t totalDown = 0;        // bytes: local -> remote (response)
    unsigned long lastActivity = millis();

    while (local.connected() && remote.connected()) {
        bool activity = false;

        // Remote -> Local (incoming data from browser)
        if (remote.available() > 0) {
            size_t len = remote.read(bufRemote, BORE_PROXY_BUFFER_SIZE);
            if (len > 0) {
                local.write(bufRemote, len);
                local.flush();
                totalUp += len;
                activity = true;
            }
        }

        // Local -> Remote (response from web server)
        if (local.available() > 0) {
            size_t len = local.read(bufLocal, BORE_PROXY_BUFFER_SIZE);
            if (len > 0) {
                size_t written = remote.write(bufLocal, len);
                remote.flush();
                totalDown += written;
                activity = true;
                Serial.printf("Bore: slot %d response %d bytes (total down: %d)\n", params->slot, written, totalDown);
            }
        }

        if (!activity) {
            vTaskDelay(pdMS_TO_TICKS(2));
            // Timeout after 30 seconds of no activity
            if (millis() - lastActivity > 30000) {
                Serial.printf("Bore: slot %d timeout\n", params->slot);
                break;
            }
        } else {
            lastActivity = millis();
        }
    }

    Serial.printf("Bore: Proxy closed, slot %d, up=%d down=%d\n", params->slot, totalUp, totalDown);
    local.stop();
    remote.stop();

    params->client->markProxySlotFree(params->slot);

    delete params;
    vTaskDelete(NULL);
}
