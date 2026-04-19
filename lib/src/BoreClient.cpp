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
    return false;
}

bool BoreClient::sendHello(uint16_t port) {
    StaticJsonDocument<BORE_MAX_FRAME_LENGTH> doc;
    doc["Hello"] = port;

    String json;
    serializeJson(doc, json);

    return sendMessage(json);
}

bool BoreClient::recvHello() {
    String msg = recvMessage();
    if (msg.isEmpty()) {
        return false;
    }

    StaticJsonDocument<BORE_MAX_FRAME_LENGTH> doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
        return false;
    }

    if (doc.containsKey("Hello")) {
        _remotePort = doc["Hello"].as<uint16_t>();
        return _remotePort > 0;
    }

    if (doc.containsKey("Error")) {
        const char* errMsg = doc["Error"];
        Serial.printf("Bore server error: %s\n", errMsg);
    }

    return false;
}

String BoreClient::recvMessage() {
    while (_control.available() > 0 && _recvPos < BORE_RECV_BUFFER_SIZE - 1) {
        char c = _control.read();

        if (c == '\0') {
            _recvBuf[_recvPos] = '\0';
            String result(_recvBuf);
            _recvPos = 0;
            return result;
        }

        _recvBuf[_recvPos++] = c;
    }

    if (_recvPos >= BORE_RECV_BUFFER_SIZE - 1) {
        _recvPos = 0;
    }

    return "";
}

bool BoreClient::sendMessage(const String& json) {
    _control.write(json.c_str(), json.length());
    _control.write('\0');
    return _control.flush();
}

void BoreClient::processMessage(const String& msg) {
    StaticJsonDocument<BORE_MAX_FRAME_LENGTH> doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
        return;
    }

    if (doc.containsKey("Heartbeat")) {
        return;
    }

    if (doc.containsKey("Connection")) {
        String uuid = doc["Connection"].as<String>();
        handleConnection(uuid);
    }

    if (doc.containsKey("Error")) {
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

    WiFiClient remote;
    WiFiClient local;

    if (!remote.connect(params->remoteHost.c_str(), BORE_CONTROL_PORT)) {
        Serial.println("Bore proxy: failed to connect remote");
        delete params;
        vTaskDelete(NULL);
        return;
    }

    remote.setTimeout(BORE_NETWORK_TIMEOUT);

    StaticJsonDocument<BORE_MAX_FRAME_LENGTH> doc;
    doc["Accept"] = params->uuid;

    String json;
    serializeJson(doc, json);

    remote.write(json.c_str(), json.length());
    remote.write('\0');
    remote.flush();

    if (!local.connect("127.0.0.1", params->localPort)) {
        Serial.println("Bore proxy: failed to connect local");
        remote.stop();
        delete params;
        vTaskDelete(NULL);
        return;
    }

    local.setTimeout(1);
    remote.setTimeout(1);

    uint8_t bufRemote[BORE_PROXY_BUFFER_SIZE];
    uint8_t bufLocal[BORE_PROXY_BUFFER_SIZE];

    while (local.connected() && remote.connected()) {
        bool activity = false;

        if (remote.available() > 0) {
            size_t len = remote.read(bufRemote, BORE_PROXY_BUFFER_SIZE);
            if (len > 0) {
                local.write(bufRemote, len);
                local.flush();
                activity = true;
            }
        }

        if (local.available() > 0) {
            size_t len = local.read(bufLocal, BORE_PROXY_BUFFER_SIZE);
            if (len > 0) {
                remote.write(bufLocal, len);
                remote.flush();
                activity = true;
            }
        }

        if (!activity) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    local.stop();
    remote.stop();

    params->client->markProxySlotFree(params->slot);

    delete params;
    vTaskDelete(NULL);
}
