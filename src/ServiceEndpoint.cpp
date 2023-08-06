#include "fluenthttp.h"

int ServiceEndpoint::connectClient() {
    int result = _client.connected();
    if (!result) {
        result = !_hasHostname 
            ? _client.connect(_ipaddr, _port)
            : _client.connect(_hostname.c_str(), _port); 
    }
    return result;
}

void ServiceEndpoint::finalizeLastRequest() {
    auto s = _lastRequest.getStatus();
    if (s != srsNotStarted && s != srsFinished) {
        _lastRequest.await();
    }
}

ServiceEndpoint::ServiceEndpoint(Client& client, const char* hostname) 
    : _client(client), _hostname(hostname), _port(80), _hasHostname(true) {
    _waitHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
}

ServiceEndpoint::ServiceEndpoint(Client& client, const char* hostname, uint16_t port) 
    : _client(client), _hostname(hostname), _port(port), _hasHostname(true) {    
    _waitHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
}

ServiceEndpoint::ServiceEndpoint(Client& client, IPAddress ip) 
    : _client(client), _ipaddr(ip), _port(80) {    
    _waitHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
}

ServiceEndpoint::ServiceEndpoint(Client& client, IPAddress ip, uint16_t port) 
    : _client(client), _ipaddr(ip), _port(port) {
    _waitHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
}

ServiceEndpoint& ServiceEndpoint::withKeepAlive() {
    _keepAlive = true;
    return *this;
}

ServiceEndpoint& ServiceEndpoint::withCloseAfterRequest() {
    _keepAlive = false;
    return *this;
}

bool ServiceEndpoint::isReady() {
    auto s = _lastRequest.getStatus();
    return (s == srsFinished || s == srsNotStarted) && !_armed;
}

bool ServiceEndpoint::lockNext() {
    const TickType_t xTicksToWait = (100) / portTICK_PERIOD_MS;
    if (xSemaphoreTake(_waitHandle, xTicksToWait) == pdTRUE)
    {
        if (isReady()) {
            _armed = true;
            return true;
        }
        xSemaphoreGive(_waitHandle);
    }
    return false;
}

ServiceRequest& ServiceEndpoint::get(const char* relativeUri) {
    finalizeLastRequest();
    _lastRequest = ServiceRequest(_client, _keepAlive);
    if (connectClient()) {
        _lastRequest.beginRequest("GET", relativeUri);
        _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : String(_ipaddr).c_str());
    }
    _armed = false;
    xSemaphoreGive(_waitHandle);
    return _lastRequest;
}

ServiceRequest& ServiceEndpoint::post(const char* relativeUri) {
    finalizeLastRequest();
    _armed = true;
    _lastRequest = ServiceRequest(_client, _keepAlive);
    if (connectClient()) {
        _lastRequest.beginRequest("POST", relativeUri);
        _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : String(_ipaddr).c_str());
    }
    _armed = false;
    xSemaphoreGive(_waitHandle);
    return _lastRequest;
}