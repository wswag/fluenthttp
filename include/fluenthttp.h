#ifndef FLUENTHTTP_H
#define FLUENTHTTP_H

#include <Arduino.h>
#include <functional>
#include <PulseGen.h>
#include <ChainedList.h>
#include <FreeRTOS.h>
//#include <RTOS.h>

#ifndef MAX_CONTENTSTRING_STACK_SIZE
  #define MAX_CONTENTSTRING_STACK_SIZE 256
#endif

struct service_response_t {
    uint16_t statusCode = 0;
    String statusMessage;
    String contentType;
    uint32_t contentLength = 0;
    Stream* contentReader;
    // TODO: Header Fields
};

enum service_request_status_t {
    srsNotStarted = 5,
    srsIncomplete = 0,
    srsAwaitResponse = 1,
    srsReadingHeader = 2,
    srsReadingContent = 3,
    srsFinished = 4
};

typedef std::function<void (service_response_t)> service_endpoint_callback_t;
typedef std::function<void ()> timeout_callback_t;

class ServiceRequest : ChainedUnidirectionalElement<ServiceRequest> {
    protected:
        service_endpoint_callback_t _successCallback = 0;
        service_endpoint_callback_t _failCallback = 0;
        timeout_callback_t _timeoutCallback = 0;
        PulseGen _timeout = PulseGen(1000, false);
        Client* _client;
        service_request_status_t _status = srsNotStarted;
        service_response_t _response = service_response_t();
        bool _keepAlive = false;

        void handleResponseBegin();
        void handleResponseHeader();
        void handleResponseContent();

    public:
        // reads back the content by evaluating the content-length attribute
        static String stringContent(service_response_t r);
        static char* cstrContent(service_response_t r);

        ServiceRequest();
        ServiceRequest(Client& s, bool keepAlive);

        ServiceRequest& onSuccess(service_endpoint_callback_t callback);
        ServiceRequest& onFailure(service_endpoint_callback_t callback);
        ServiceRequest& onTimeout(timeout_callback_t callback);
        ServiceRequest& withKeepAlive();
        ServiceRequest& withTimeout(uint32_t timeout);

        void yield();

        void beginRequest(const char* method, const char* relativeUri);
        ServiceRequest& addHeader(const char* key, const char* value);
        ServiceRequest& fire();
        ServiceRequest& fireContent(size_t count, uint8_t* data);
        ServiceRequest& fireContent(String data);

        void await();

        service_request_status_t getStatus();
};

class ServiceEndpoint : ChainedList<ServiceRequest> {
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

        int connectClient();
        void finalizeLastRequest();
    public:
        ServiceEndpoint(Client& client, const char* hostname);
        ServiceEndpoint(Client& client, const char* hostname, uint16_t port);
        ServiceEndpoint(Client& client, IPAddress ip);
        ServiceEndpoint(Client& client, IPAddress ip, uint16_t port);

        ServiceEndpoint& withKeepAlive();
        ServiceEndpoint& withCloseAfterRequest();

        bool isReady();
        bool lockNext();
        ServiceRequest& get(const char* relativeUri);
        ServiceRequest& post(const char* relativeUri);

        IPAddress getIPAdress() { return _ipaddr; }
        const char* getHostname() { return _hostname.c_str(); }
};


#endif /* FLUENTHTTP_H */
