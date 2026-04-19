#ifndef BORE_CLIENT_H
#define BORE_CLIENT_H

#include <Arduino.h>
#include <WiFiClient.h>

#define BORE_CONTROL_PORT 7835
#define BORE_MAX_FRAME_LENGTH 256
#define BORE_NETWORK_TIMEOUT 3000
#define BORE_MAX_PROXY_CONNECTIONS 3
#define BORE_RECV_BUFFER_SIZE 256
#define BORE_PROXY_BUFFER_SIZE 512

class BoreClient {
public:
    BoreClient();
    ~BoreClient();

    // Connect ke bore server, kirim Hello
    // remoteHost: hostname/IP bore server (e.g. "bore.pub")
    // localPort: port lokal yang ingin di-expose
    // requestedPort: port publik yang diminta (0 = random)
    bool begin(const char* remoteHost, uint16_t localPort, uint16_t requestedPort = 0);

    // Process incoming control messages (panggil di loop)
    void loop();

    // Port publik yang di-assign server (0 jika belum connected)
    uint16_t remotePort() const { return _remotePort; }

    // Status koneksi control
    bool isConnected() const { return _connected; }

    // Stop & cleanup semua koneksi
    void stop();

private:
    // Control connection
    WiFiClient _control;
    String _remoteHost;
    uint16_t _localPort;
    uint16_t _remotePort;
    bool _connected;

    // Buffer untuk recv null-delimited JSON
    char _recvBuf[BORE_RECV_BUFFER_SIZE];
    size_t _recvPos;

    // Proxy connection tracking
    struct ProxyConnection {
        TaskHandle_t task;
        bool active;
    };
    ProxyConnection _proxies[BORE_MAX_PROXY_CONNECTIONS];

    // Internal methods
    bool connectToServer();
    bool sendHello(uint16_t port);
    bool recvHello();
    void handleConnection(const String& uuid);
    bool sendMessage(const String& json);
    String recvMessage();
    void processMessage(const String& msg);
    int findFreeProxySlot();
    void markProxySlotFree(int slot);

    // Proxy task function
    struct ProxyTaskParams {
        BoreClient* client;
        String remoteHost;
        String uuid;
        uint16_t localPort;
        int slot;
    };
    static void proxyTask(void* param);

    friend class BoreClientTest;
};

#endif // BORE_CLIENT_H
