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

void ServiceEndpoint::createSemaphores() {
    _waitHandle = xSemaphoreCreateBinary();
    xSemaphoreGive(_waitHandle);
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

bool ServiceEndpoint::unlock() {
    xSemaphoreGive(_waitHandle);
    return true;
}

void ServiceEndpoint::forceUnlock() {
    close();
    unlock();
}

void ServiceEndpoint::close() {
    _client->stop();
}

bool ServiceEndpoint::beginRequest(const char* relativeUri, const char* httpMethod, ServiceRequest& request, int lockTimeout) {
    // acquire the semaphore first
    if (xSemaphoreTake(_waitHandle, lockTimeout) == pdFALSE)
        return false;

    request = ServiceRequest(_client, this);
    if (!connectClient()) {
        request.fail("failed to connect to server");
        return true;
    }
    request.beginRequest();
    request.call(httpMethod, relativeUri);
    request.addHeader("Host", _hasHostname ? _hostname.c_str() : _ipaddr.toString().c_str());
    request.addHeader("Accept", "*/*");
    request.addHeader("Connection", _keepAlive ? "keep-alive" : "close");
    request.withKeepAlive(_keepAlive);
    return true;
}

bool ServiceEndpoint::get(const char* relativeUri, ServiceRequest& request, int lockTimeout) {
    return beginRequest(relativeUri, "GET", request, lockTimeout);
}

bool ServiceEndpoint::post(const char* relativeUri, ServiceRequest& request, int lockTimeout) {
    return beginRequest(relativeUri, "POST", request, lockTimeout);
}