#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include <fluenthttp.h>
#include <env.h>

WiFiClient client;
ServiceEndpoint endpoint(client, TEST_ENDPOINT);

const int TIMEOUT = 500;

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
  // clean stuff up here
}

void print_content(service_response_t r) {
  char buffer[256];
  printf("content-length: %d\r\n", r.contentLength);
  int cursor = 0;
  while (cursor < r.contentLength) {    
    auto nextChunk = min(r.contentReader->available(), (int)sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    int i = r.contentReader->readBytes(buffer, nextChunk);
    cursor += i;
    printf(buffer);
  }
  printf("\r\n");
}

void get_request(int timeout, bool sync) {
  ServiceRequest& request = endpoint.get("/publickey/");
  request.withTimeout(timeout)
      .onSuccess([=](service_response_t r) {
          print_content(r);
      })
      .onFailure([=](service_response_t r) {
        printf("request failed with code %d: %s\r\n", r.statusCode, r.statusMessage.c_str());
        // leads to stack overflow..
          //TEST_FAIL_MESSAGE("request failed");
      })
      .onTimeout([=] {
        printf("request timed out\r\n");
        // leads to stack overflow..
         // TEST_FAIL_MESSAGE("request timed out");
      })
      .fire();
    if (sync)
      request.await();
}

void test_sync_implicit_await_calls_with_keepalive() {
    endpoint.withKeepAlive(true);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, false);
    }
}

void test_sync_implicit_await_calls_with_close() {
    endpoint.withKeepAlive(false);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, false);
        endpoint.close();
    }
}

void test_sync_explicit_await_calls_with_keepalive() {
    endpoint.withKeepAlive(true);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, true);
    }
}

void test_sync_explicit_await_calls_with_close() {
  endpoint.withKeepAlive(false);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, true);
        endpoint.close();
    }
}

bool doParallel = false;

void yield_task(void* arg) {
  while (true)
  {
    ServiceRequest& r = endpoint.getActiveRequest();
    if (r.getStatus() != srsIdle) {
      r.yield();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void parallel_task(void* arg) {
  while (true)
  {
    if (doParallel)
      test_sync_implicit_await_calls_with_close();
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void test_parallel_explicit_await_calls() {
  doParallel = true;
  test_sync_explicit_await_calls_with_close();
  doParallel = false;
}

void test_timeout_continue() {
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with success expected\r\n");
    get_request(500, true);
}

void test_failure_continue() {
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with timeout expected\r\n");
    get_request(1, true);
    printf("request with success expected\r\n");
    get_request(500, true);
}

void setup()
{
  // NOTE!!! Wait for >2 secs
  // if board doesn't support software reset via Serial.DTR/RTS
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);

  UNITY_BEGIN(); // IMPORTANT LINE!

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (!WiFi.isConnected()) {
    digitalWrite(LED_BUILTIN, (digitalRead(LED_BUILTIN) + 1) % 2);
    delay(300);
  }

  RUN_TEST(test_timeout_continue);

  TaskHandle_t handle1, handle2;
  xTaskCreate(parallel_task, "t1", 8192, nullptr, 5, &handle1);
  xTaskCreate(yield_task, "t2", 32000, nullptr, 5, &handle2);

  RUN_TEST(test_sync_explicit_await_calls_with_close);
  RUN_TEST(test_sync_implicit_await_calls_with_close);
  RUN_TEST(test_sync_explicit_await_calls_with_keepalive);
  RUN_TEST(test_sync_implicit_await_calls_with_keepalive);
  
}

void loop()
{
  static int i = 0;
  RUN_TEST(test_parallel_explicit_await_calls);
  if (i >= 100)
    UNITY_END(); // stop unit testing
  i++;
}
