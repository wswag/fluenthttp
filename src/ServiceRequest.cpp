#include "fluenthttp.h"

// TODO: ServiceRequest nicht mehr als Referenz liefern, die vom Service überprüft wird,
// sondern als Copy-Objekt und im Objekt die Semaphore im Service steuern
// Problem: Dangling References

int service_response_t::nextChunk() {
    if (this->contentReader->available() == 0) return 0;
    if (!this->chunked) {
        return this->contentLength;
    }
    else
    {
        String chunkLenHex;
        do {
            if (this->contentReader->available() == 0) return 0;
            chunkLenHex = this->contentReader->readStringUntil('\n');
            chunkLenHex.trim();
            chunkLenHex.toUpperCase();
        } while (chunkLenHex.length() == 0);
        int chunkLen = strtol(chunkLenHex.c_str(), nullptr, 16);
        return chunkLen;
    }
}

void ServiceRequest::beginRequest() {
    _status = srsArmed;
}

void ServiceRequest::fail(const char* message)
{
    bool wasntUninitialized = _status != srsUninitialized;
    _status = srsPrefailed;
    _response = service_response_t();
    _response.statusMessage = message;
    if (wasntUninitialized) {
        // trigger failed handler immediately
        fire();
    }
}

void ServiceRequest::call(const char* method, const char* relativeUri) 
{
    if (_status != srsArmed)
        return;
    //log_w("[%X]!> call vs %X", ((size_t)this), _client);
    _status = srsIncomplete;
    // check if client is connected, connect otherwise
    _client->print(method);
    _client->print(' ');
    _client->print(relativeUri); // TODO: URL Encode
    _client->println(" HTTP/1.1");

    //Serial.printf("[%X]> ", (uint8_t)((size_t)this));
    //Serial.print(method);
    //Serial.print(' ');
    //Serial.print(relativeUri); // TODO: URL Encode
    //Serial.println(" HTTP/1.1");
}

void ServiceRequest::finalize(service_request_status_t status)
{
    //log_w("[%X]!> finalize vs %X", ((size_t)this), _client);
    _status = status;
    if (!_keepAlive && _client != nullptr) {
        _client->stop();
    }
}

void ServiceRequest::handleResponseBegin() 
{
    String line = _client->readStringUntil('\n'); // next line
    //Serial.printf("[%X]> %s", (uint8_t)((size_t)this), line.c_str());
    unsigned int statusCode = 0;
    int httpSubversion = 0;
    sscanf(line.c_str(), "HTTP/1.%u %u", &httpSubversion, &statusCode);
    if (statusCode != 0) {
        _response.statusMessage = line.substring(12);
        _response.statusMessage.trim();
        _response.statusCode = statusCode;
        _status = srsReadingHeader;
        if (httpSubversion == 0) {
            _keepAlive = false; // close after request
        }
    }
}

void ServiceRequest::handleResponseHeader() {
    int peek = _client->peek();
    //Serial.printf("[%X]> ", (uint8_t)((size_t)this));
    if (peek == '\r' || peek == '\n') {
        // header ends, content starts
        _client->readStringUntil('\n');
        //Serial.println();
        _status = srsReadingContent;
        return;
    }

    // read next header field
    String key = _client->readStringUntil(':');
    String val = _client->readStringUntil('\n');
    //Serial.printf("%s: %s\r\s", key.c_str(), val.c_str());
    val.trim();
    
    if (key == "Content-Length") {
        _response.contentLength = atoi(val.c_str());
    }
    else if (key == "Content-Type") {
        _response.contentType = val;
    }
    else if (key == "Transfer-Encoding") {
        /*
        Transfer-Encoding: chunked
        Transfer-Encoding: compress
        Transfer-Encoding: deflate
        Transfer-Encoding: gzip

        // Several values can be listed, separated by a comma
        Transfer-Encoding: gzip, chunked

        For now: only support chunked mode...
        */

       if (val == "chunked") {
         _response.chunked = true;
       } else {
        fail(("server side Transfer-Encoding not supported: " + val).c_str());
       }
    }
}

void ServiceRequest::handleResponseContent() {
    _response.contentReader = _client;
    if (_response.statusCode >= 400) {
        if (_failCallback != 0) {
            _failCallback(_response);
        }
        finalize(srsFailed);
    }
    else {
        if (_successCallback != 0) {
            _successCallback(_response);
        }
        finalize(srsCompleted);
    }
}

// reads back the content by evaluating the content-length attribute
String ServiceRequest::stringContent(service_response_t r) {
    if (r.contentLength == 0)
        return r.contentReader->readString(); // fallback

    String result = String((char*) 0);
    uint32_t n = r.contentLength;
    result.reserve(n);
    char buf[MAX_CONTENTSTRING_STACK_SIZE+1];
    while (n > 0) {
        // get byte chunks of defined size max
        uint32_t nextChunk = n > MAX_CONTENTSTRING_STACK_SIZE
            ? MAX_CONTENTSTRING_STACK_SIZE
            : n;
        buf[nextChunk] = 0; // terminator
        r.contentReader->readBytes(buf, r.contentLength);
        result += buf;
        n -= nextChunk;
    }
    return result;
}

char* ServiceRequest::cstrContent(service_response_t r) {
    if (r.contentLength == 0) return 0;
    char* result = (char*)malloc(r.contentLength+1);
    result[r.contentLength] = 0; // terminator
    r.contentReader->readBytes(result, r.contentLength);
    return result;
}

ServiceRequest::ServiceRequest() 
        : _client(nullptr) {
}

ServiceRequest::ServiceRequest(Client* s, ServiceEndpoint* endpoint) 
        : _client(s), _endpoint(endpoint) {

}

ServiceRequest& ServiceRequest::onSuccess(service_endpoint_callback_t callback) {
    _successCallback = callback;
    return *this;
}

ServiceRequest& ServiceRequest::onFailure(service_endpoint_callback_t callback) {
    _failCallback = callback;
    return *this;
}

ServiceRequest& ServiceRequest::onTimeout(timeout_callback_t callback) {
    _timeoutCallback = callback;
    return *this;
}

ServiceRequest& ServiceRequest::withTimeout(uint32_t timeout) {
    _timeout = timeout;
    return *this;
}

bool ServiceRequest::yield() {
    // block parallel yield calls
    switch (_status) {
        case srsUninitialized:
        case srsArmed:
        case srsIncomplete:
            return false;
        case srsCompleted:
        case srsFailed:
            return true;
    }

    try {
        innerYield();
    }
    catch (std::exception e)
    {
        // something failed on user code
        cancel(e.what());
    }
    
    if (finished()) {
        // unlock the service
        _endpoint->unlock();
        return true;
    }
    return false;
}

void ServiceRequest::innerYield()
{
    size_t btr = 0;
    while ((btr = _client->available()) != 0 || 
            // trigger callback when contentlength was not specified or 0
            (_status == srsReadingContent && _response.contentLength == 0)) {
        switch (_status) {
            case srsAwaitResponse: handleResponseBegin(); break;
            case srsReadingHeader: handleResponseHeader(); break;
            // content with length >0
            case srsReadingContent:
                handleResponseContent();
                return;
            default:
                return;
        }
    }

    // check timeout
    if  (_timeout != 0 && (millis() - _t0) >= _timeout) {
        if (_timeoutCallback != 0)
            _timeoutCallback();
        finalize(srsFailed);
        return;
    }
}

ServiceRequest& ServiceRequest::addHeader(const char* key, const char* value) {
    if (_status != srsIncomplete) return *this;
    _client->print(key);
    _client->print(": ");
    _client->println(value);

    //Serial.printf("[%X]> ", (uint8_t)((size_t)this));
    //Serial.print(key);
    //Serial.print(": ");
    //Serial.println(value);
    return *this;
}

ServiceRequest& ServiceRequest::fire() {
    if (_status == srsPrefailed) {
        // call failed callback directly
        try {
            if (_failCallback != 0)
                _failCallback(_response);
        }
        catch (std::exception e) {
        }
        finalize(srsFailed);
    }
    else if (_status == srsIncomplete) {
        _client->println();
         //Serial.printf("[%X]> ", (uint8_t)((size_t)this));
         //Serial.println();
        _t0 = millis();
        _status = srsAwaitResponse;
    }
    return *this;
}

ServiceRequest& ServiceRequest::fireContent(size_t count, uint8_t* data) {
    addHeader("Content-Length", String(count).c_str());
    _client->println();
    _client->write(data, count);
    _t0 = millis();
    _status = srsAwaitResponse;
    return *this;
}

ServiceRequest& ServiceRequest::fireContent(String data) {
    addHeader("Content-Length", String(data.length()).c_str());
    _client->println();
    _client->print(data);
    _t0 = millis();
    _status = srsAwaitResponse;
    return *this;
}

void ServiceRequest::cancel(const char* message) {
    try {
        if (finished()) return;
        // something failed on user code
        fail(message);
    }
    catch (std::exception e) {}
}

void ServiceRequest::await() {
    switch (_status) {
        case srsUninitialized:
        case srsArmed:
        case srsIncomplete:
            return;
        case srsPrefailed:
            fire();
        default: break;
    }
    
    while (!yield())
    {
        delay(10); // will call vTaskDelay on FreeRTOS platforms to allow other tasks to run
    }
}

service_request_status_t ServiceRequest::getStatus() {
    return _status;
}