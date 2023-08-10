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

ServiceRequest& ServiceEndpoint::getActiveRequest() {
    return _lastRequest;
}

void ServiceEndpoint::createSemaphores() {
    _waitHandle = xSemaphoreCreateBinary();
    _yieldHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
    xSemaphoreGive(_yieldHandle);
}

ServiceEndpoint::ServiceEndpoint(Client& client, const char* hostname) 
    : _client(client), _hostname(hostname), _port(80), _hasHostname(true) {
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(Client& client, const char* hostname, uint16_t port) 
    : _client(client), _hostname(hostname), _port(port), _hasHostname(true) {    
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(Client& client, IPAddress ip) 
    : _client(client), _ipaddr(ip), _port(80) {    
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(Client& client, IPAddress ip, uint16_t port) 
    : _client(client), _ipaddr(ip), _port(port) {
    createSemaphores();
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
    return (s == srsIdle);
}

bool ServiceEndpoint::lockNext(int msToWait) {
    bool result = false;
    const TickType_t xTicksToWait = (msToWait) / portTICK_PERIOD_MS;
    if (xSemaphoreTake(_waitHandle, xTicksToWait) == pdTRUE)
    {
        if (isReady() && connectClient()) {
            result = true;
            _lastRequest = ServiceRequest(_client, _keepAlive, _yieldHandle);
            _lastRequest.beginRequest(_nonce);
            _nonce++;
        }
        xSemaphoreGive(_waitHandle);
    }
    return result;
}

void ServiceEndpoint::assertNonce() {
    if (_lastRequest.getStatus() == srsIdle && _nonce != _lastRequest._nonce) {
        while (!lockNext(100))
            vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

ServiceRequest& ServiceEndpoint::get(const char* relativeUri) {
    assertNonce();
    _lastRequest.call("GET", relativeUri);
    _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : String(_ipaddr).c_str());
    return _lastRequest;
}

ServiceRequest& ServiceEndpoint::post(const char* relativeUri) {
    assertNonce();
    _lastRequest.call("POST", relativeUri);
    _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : String(_ipaddr).c_str());
    return _lastRequest;
}