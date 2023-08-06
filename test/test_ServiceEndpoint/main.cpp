#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include <fluenthttp.h>

WiFiClient client;
ServiceEndpoint endpoint(client, "test.wagner-mendoza.de");

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

void test_sync_implicit_await_calls_with_keepalive() {
    endpoint.withKeepAlive();
    for (int k = 0; k <= 10; k++) {
        ServiceRequest& request = endpoint.get("/json");
        request.withTimeout(500)
            .onSuccess([=](service_response_t r) {
                TEST_ASSERT(true); 
                print_content(r);
            })
            .onFailure([=](service_response_t r) {
                TEST_FAIL_MESSAGE("request failed");
            })
            .onTimeout([&] {
                TEST_FAIL_MESSAGE("request timed out");
            })
            .fire();
    }
}

void test_sync_implicit_await_calls_with_close() {
    endpoint.withCloseAfterRequest();
    for (int k = 0; k <= 10; k++) {
        ServiceRequest& request = endpoint.get("/json");
        request.withTimeout(500)
            .onSuccess([=](service_response_t r) {
                TEST_ASSERT(true); 
                print_content(r);
            })
            .onFailure([=](service_response_t r) {
                TEST_FAIL_MESSAGE("request failed");
            })
            .onTimeout([&] {
                TEST_FAIL_MESSAGE("request timed out");
            })
            .fire();
    }
}

void test_sync_explicit_await_calls_with_keepalive() {
    endpoint.withKeepAlive();
    for (int k = 0; k <= 10; k++) {
        ServiceRequest& request = endpoint.get("/json");
        request.withTimeout(500)
            .onSuccess([=](service_response_t r) {
                TEST_ASSERT(true); 
                print_content(r);
            })
            .onFailure([=](service_response_t r) {
                TEST_FAIL_MESSAGE("request failed");
            })
            .onTimeout([&] {
                TEST_FAIL_MESSAGE("request timed out");
            })
            .fire()
            .await();
    }
}

void test_sync_explicit_await_calls_with_close() {
    endpoint.withCloseAfterRequest();
    for (int k = 0; k <= 10; k++) {
        ServiceRequest& request = endpoint.get("/json");
        request.withTimeout(500)
            .onSuccess([=](service_response_t r) {
                TEST_ASSERT(true); 
                print_content(r);
            })
            .onFailure([=](service_response_t r) {
                TEST_FAIL_MESSAGE("request failed");
            })
            .onTimeout([&] {
                TEST_FAIL_MESSAGE("request timed out");
            })
            .fire()
            .await();
    }
}


bool doParallel = false;

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
  test_sync_implicit_await_calls_with_close();
}

void setup()
{
  // NOTE!!! Wait for >2 secs
  // if board doesn't support software reset via Serial.DTR/RTS
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);

  UNITY_BEGIN(); // IMPORTANT LINE!

  WiFi.mode(WIFI_STA);
  WiFi.begin("H28-EG", "VaSia14$wusT");
  while (!WiFi.isConnected()) {
    digitalWrite(LED_BUILTIN, (digitalRead(LED_BUILTIN) + 1) % 2);
    delay(300);
  }

  TaskHandle_t handle1;
  xTaskCreate(parallel_task, "t1", 8192, nullptr, 5, &handle1);

  RUN_TEST(test_sync_explicit_await_calls_with_close);
  RUN_TEST(test_sync_implicit_await_calls_with_close);
  RUN_TEST(test_sync_explicit_await_calls_with_keepalive);
  RUN_TEST(test_sync_implicit_await_calls_with_keepalive);
  RUN_TEST(test_parallel_explicit_await_calls);
  UNITY_END(); // stop unit testing
}

void loop()
{
}
