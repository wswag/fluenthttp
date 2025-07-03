#include "fluenthttp.h"

int ServiceEndpoint::connectClient() {
    int result = _client->connected();
    if (!result) {
        result = !_hasHostname 
            ? _client->connect(_ipaddr, _port)
            : _client->connect(_hostname.c_str(), _port); 
    }
    else
    {
        // read all available data to start clean
        while (_client->available()) _client->read(); // TODO: how to make this more efficient?
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

ServiceEndpoint::ServiceEndpoint(const char* hostname) 
    : _hostname(hostname), _port(80), _hasHostname(true) {
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(const char* hostname, uint16_t port) 
    : _hostname(hostname), _port(port), _hasHostname(true) {    
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(IPAddress ip) 
    : _ipaddr(ip), _port(80) {    
    createSemaphores();
}

ServiceEndpoint::ServiceEndpoint(IPAddress ip, uint16_t port) 
    : _ipaddr(ip), _port(port) {
    createSemaphores();
}

ServiceEndpoint& ServiceEndpoint::withKeepAlive(bool keepAliveHeader) {
    _keepAlive = keepAliveHeader;
    return *this;
}

void ServiceEndpoint::begin(Client* client) {
    _client = client;
}

bool ServiceEndpoint::isReady() {
    auto s = _lastRequest.getStatus();
    return (s == srsIdle || _lastRequest.finished() && _client != nullptr);
}

int ServiceEndpoint::lockNext(int msToWait) {
    const TickType_t xTicksToWait = (msToWait) / portTICK_PERIOD_MS;
    if (xSemaphoreTake(_waitHandle, xTicksToWait) == pdTRUE)
    {
        _lastRequest = ServiceRequest(nullptr, this);
        _lastRequest.beginRequest(_nonce);
        return _nonce;
    }
    return -1;
}

bool ServiceEndpoint::unlock(int nonce) {
    if (nonce == _nonce) {
        if (!isReady()) {
            _lastRequest.cancel("unlock cancel");
        }
        _nonce++;
        xSemaphoreGive(_waitHandle);
        return true;
    }
    return false;
}

void ServiceEndpoint::forceUnlock() {
    unlock(_nonce);
}

void ServiceEndpoint::close() {
    if (!isReady()) {
        _lastRequest.cancel("force close");
    }
    else {
        _client->stop();
    }
}

void ServiceEndpoint::assertNonce(int nonce) {
    if (_lastRequest._nonce != nonce) {
        while (lockNext(1000) == -1)
        {
        }
    }
}

ServiceRequest& ServiceEndpoint::get(const char* relativeUri, int nonce) {
    assertNonce(nonce);
    if (!connectClient()) {
        _lastRequest.fail("failed to connect to server");
        return _lastRequest;
    }
    _lastRequest._client = _client;
    _lastRequest.call("GET", relativeUri);
    _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : _ipaddr.toString().c_str());
    _lastRequest.addHeader("Accept", "*/*");
    _lastRequest.addHeader("Connection", _keepAlive ? "keep-alive" : "close");
    _lastRequest.withKeepAlive(_keepAlive);
    return _lastRequest;
}

ServiceRequest& ServiceEndpoint::post(const char* relativeUri, int nonce) {
    assertNonce(nonce);
    if (!connectClient()) {
        _lastRequest.fail("failed to connect to server");
        return _lastRequest;
    }
    _lastRequest._client = _client;
    _lastRequest.call("POST", relativeUri);
    _lastRequest.addHeader("Host", _hasHostname ? _hostname.c_str() : _ipaddr.toString().c_str());
    _lastRequest.addHeader("Accept", "*/*");
    _lastRequest.addHeader("Connection", _keepAlive ? "keep-alive" : "close");
    return _lastRequest;
}