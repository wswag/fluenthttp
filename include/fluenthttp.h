#ifndef FLUENTHTTP_H
#define FLUENTHTTP_H

#include <Arduino.h>
#include <functional>
#include <RTOS.h>

#ifndef MAX_CONTENTSTRING_STACK_SIZE
  #define MAX_CONTENTSTRING_STACK_SIZE 256
#endif

struct service_response_t {
    uint16_t statusCode = 0;
    String statusMessage;
    String contentType;
    uint32_t contentLength = 0;
    Stream* contentReader = nullptr;
    // TODO: Header Fields
};

enum service_request_status_t {
    srsIdle = 0,
    srsArmed = 1,
    srsIncomplete = 2,
    srsAwaitResponse = 3,
    srsReadingHeader = 4,
    srsReadingContent = 5,
    srsCompleted = 6,
    srsPrefailed = 8,
    srsFailed = 8
};

typedef std::function<void (service_response_t)> service_endpoint_callback_t;
typedef std::function<void ()> timeout_callback_t;

class ServiceRequest {
    friend class ServiceEndpoint;
    private:
        SemaphoreHandle_t _yieldHandle; // will be pushed from endpoint

        service_endpoint_callback_t _successCallback = 0;
        service_endpoint_callback_t _failCallback = 0;
        timeout_callback_t _timeoutCallback = 0;
        long _t0 = 0;
        int _timeout = 1000;
        Client* _client;
        service_request_status_t _status = srsIdle;
        service_response_t _response = service_response_t();
        long _nonce = 0;

        void handleResponseBegin();
        void handleResponseHeader();
        void handleResponseContent();

        void beginRequest(long nonce);
        void call(const char* method, const char* relativeUri);
        void innerYield();

        ServiceRequest(Client& s, SemaphoreHandle_t yieldHandle);
        void fail(const char* message);
    public:
        // reads back the content by evaluating the content-length attribute
        static String stringContent(service_response_t r);
        static char* cstrContent(service_response_t r);

        ServiceRequest();
        
        ServiceRequest& onSuccess(service_endpoint_callback_t callback);
        ServiceRequest& onFailure(service_endpoint_callback_t callback);
        ServiceRequest& onTimeout(timeout_callback_t callback);
        ServiceRequest& withKeepAlive();
        ServiceRequest& withTimeout(uint32_t timeout);

        
        ServiceRequest& addHeader(const char* key, const char* value);
        ServiceRequest& fire();
        ServiceRequest& fireContent(size_t count, uint8_t* data);
        ServiceRequest& fireContent(String data);

        void cancel();
        void await();
        bool yield();

        service_request_status_t getStatus();
};

class ServiceEndpoint {
    private:
        Client& _client;
        String _hostname;
        IPAddress _ipaddr;
        uint16_t _port;
        bool _hasHostname = false;
        ServiceRequest _lastRequest;
        bool _armed = false;
        bool _keepAlive = false;

        SemaphoreHandle_t _waitHandle;
        SemaphoreHandle_t _yieldHandle;

        long _nonce = 1;

        int connectClient();
        void assertNonce();
        void createSemaphores();
    public:
        ServiceEndpoint(Client& client, const char* hostname);
        ServiceEndpoint(Client& client, const char* hostname, uint16_t port);
        ServiceEndpoint(Client& client, IPAddress ip);
        ServiceEndpoint(Client& client, IPAddress ip, uint16_t port);

        ServiceEndpoint& withKeepAlive(bool keepAliveHeader);

        // close the underlying client
        void close();

        bool isReady();
        bool lockNext(int msToWait);
        ServiceRequest& get(const char* relativeUri);
        ServiceRequest& post(const char* relativeUri);

        IPAddress getIPAdress() { return _ipaddr; }
        const char* getHostname() { return _hostname.c_str(); }

        ServiceRequest& getActiveRequest();
};


#endif /* FLUENTHTTP_H */
