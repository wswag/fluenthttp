#include "fluenthttp.h"

void ServiceRequest::beginRequest(const char* method, const char* relativeUri) 
{
    // check if client is connected, connect otherwise
    _status = srsIncomplete;
    _client->print(method);
    _client->print(' ');
    _client->print(relativeUri); // TODO: URL Encode
    _client->println(" HTTP/1.1");
    addHeader("Connection", _keepAlive ? "keep-alive" : "close");
}

void ServiceRequest::handleResponseBegin() 
{
    String line = _client->readStringUntil('\n'); // next line
    unsigned int statusCode = 0;
    sscanf(line.c_str(), "HTTP/1.1 %u", &statusCode);
    if (statusCode != 0) {
        _response.statusMessage = line.substring(12);
        _response.statusMessage.trim();
        _response.statusCode = statusCode;
        _status = srsReadingHeader;
    }
}

void ServiceRequest::handleResponseHeader() {
    int peek = _client->peek();
    if (peek == '\r' || peek == '\n') {
        // header ends, content starts
        _client->read();
        int peek = _client->peek();
        if (peek == '\r' || peek == '\n')
            _client->read(); // this should be normal
        _status = srsReadingContent;
        return;
    }

    // read next header field
    String key = _client->readStringUntil(':');
    String val = _client->readStringUntil('\n');
    val.trim();
    
    if (key == "Content-Length") {
        _response.contentLength = atoi(val.c_str());
    }
    else if (key == "Content-Type") {
        _response.contentType = val;
    }
}

void ServiceRequest::handleResponseContent() {
    _response.contentReader = _client;
    if (_response.statusCode >= 400) {
        if (_failCallback != 0)
            _failCallback(_response);
    }
    else {
        if (_successCallback != 0) {
            _successCallback(_response);
        }
    }
    if (!_keepAlive) {
        _client->stop();
    }
    else
        while (_client->available()) _client->read(); // TODO: how to make this more efficient?
    _status = srsFinished;
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

ServiceRequest::ServiceRequest(Client& s, bool keepAlive) 
        : _client(&s), _keepAlive(_keepAlive) {
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
    _timeout = PulseGen(timeout, false);
    return *this;
}

void ServiceRequest::yield() {
    if (_status == srsIncomplete || _status == srsFinished) return;

    // check timeout
    if  (_timeout.getInterval() != 0 && _timeout.Pulse()) {
        _status = srsFinished;
        if (_timeoutCallback != 0)
            _timeoutCallback();
    }
    
    size_t btr = 0;
    while ((btr = _client->available()) != 0 || 
            // trigger callback when contentlength was not specified or 0
            (_status == srsReadingContent && _response.contentLength == 0)) {
        switch (_status) {
            case srsAwaitResponse: handleResponseBegin(); break;
            case srsReadingHeader: handleResponseHeader(); break;
            // content with length >0
            case srsReadingContent: handleResponseContent(); break;
            default: return;
        }
    }
}

ServiceRequest& ServiceRequest::addHeader(const char* key, const char* value) {
    if (_status != srsIncomplete) return *this;
    _client->print(key);
    _client->print(": ");
    _client->println(value);
    return *this;
}

ServiceRequest& ServiceRequest::fire() {
    if (_status == srsIncomplete) {
        _client->println();
        _timeout.Reset();
        _status = srsAwaitResponse;
    }
    return *this;
}

ServiceRequest& ServiceRequest::fireContent(size_t count, uint8_t* data) {
    addHeader("Content-Length", String(count).c_str());
    _client->println();
    _client->write(data, count);
    _timeout.Reset();
    _status = srsAwaitResponse;
    return *this;
}

ServiceRequest& ServiceRequest::fireContent(String data) {
    addHeader("Content-Length", String(data.length()).c_str());
    _client->println();
    _client->print(data);
    _timeout.Reset();
    _status = srsAwaitResponse;
    return *this;
}

void ServiceRequest::await() {
    fire(); // to be sure.. won't have side effects if already fired
    while (_status != srsFinished) {
        vTaskDelay(10 / portTICK_RATE_MS);
        yield();
    }
}

service_request_status_t ServiceRequest::getStatus() {
    return _status;
}