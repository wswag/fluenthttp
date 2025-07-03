#include <Arduino.h>
#include <unity.h>
#include <fluenthttp.h>
#include <env.h>

#ifdef WIFI_CLIENT
#include <WiFi.h>
#include <BufferlessWiFiClient.h>
BufferlessWiFiClient client;
#else
#include <Ethernet.h>
#include <EthernetClient.h>
EthernetClient client;
#endif


ServiceEndpoint endpoint(TEST_ENDPOINT);

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
  static int n = 0;
  n++;
  printf("get request %d\r\n", n);
  ServiceRequest& request = endpoint.get("/publickey/");
  bool success = false;
  bool* successPtr = &success;
  request.withTimeout(timeout)
      .onSuccess([=](service_response_t r) {
          *successPtr = true;
          printf("request %d succeeded at %d, content length %d\r\n", n, millis(), r.contentLength);
          //print_content(r);
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
      });
    printf("fire request %d\r\n", n);
    request.fire();
    if (sync) {
      printf("await request %d\r\n", n);
      request.await();
      TEST_ASSERT_TRUE(success);
    }
}

void get_request_chunked(int timeout, bool sync) {
  ServiceRequest& request = endpoint.get("/firmware/Tester/image/chunk?start=0&len=4096");
  bool success = false;
  bool* successPtr = &success;
  request.withTimeout(timeout)
      .onSuccess([=](service_response_t r) {
          *successPtr = true;
          int chunk = r.nextChunk();
          byte buffer[4096];
          int n = 1;
          while (chunk != 0) {
            printf("chunk %d: %d bytes\r\n", n, chunk);
            r.contentReader->readBytes(buffer, chunk);
            log_print_buf(buffer, chunk);
            n++;
            chunk = r.nextChunk();
          }
          printf("request succeeded\r\n");
          //print_content(r);
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
    if (sync) {
      request.await();
      TEST_ASSERT_TRUE(success);
    }
}

void test_sync_implicit_await_calls_with_keepalive() {
    endpoint.withKeepAlive(false);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, true);
    }
}

void test_sync_implicit_await_calls_with_close() {
    endpoint.withKeepAlive(false);
    for (int k = 0; k <= 10; k++) {
        get_request(TIMEOUT, false);
        //endpoint.close();
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
        //endpoint.close();
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

void test_chunked_transferencoding() {
  get_request_chunked(1000, true);
}

void setup()
{
  // NOTE!!! Wait for >2 secs
  // if board doesn't support software reset via Serial.DTR/RTS
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);

  UNITY_BEGIN(); // IMPORTANT LINE!

  #ifdef WIFI_CLIENT
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (!WiFi.isConnected()) {
    digitalWrite(LED_BUILTIN, (digitalRead(LED_BUILTIN) + 1) % 2);
    delay(300);
  }
  #else
  log_d("initializing ethernet on cs pin %d", ETHERNET_SPI_CS_PIN);
  Ethernet.init(ETHERNET_SPI_CS_PIN);
  uint8_t mac[6] = ETHERNET_MAC_ADDRESS;
  if (!Ethernet.begin(mac)) {
    log_e("eth connection failed");
  }
  #endif

  endpoint.begin(&client);

  TaskHandle_t handle1, handle2, handle3, yieldHandle;
  xTaskCreate(parallel_task, "t1", 12000, nullptr, 5, &handle1);
  xTaskCreate(parallel_task, "t2", 12000, nullptr, 5, &handle2);
  xTaskCreate(parallel_task, "t3", 12000, nullptr, 5, &handle3);
  xTaskCreate(yield_task, "t2", 32000, nullptr, 5, &yieldHandle);

  RUN_TEST(test_chunked_transferencoding);
  RUN_TEST(test_sync_explicit_await_calls_with_close);
  RUN_TEST(test_sync_implicit_await_calls_with_close);
  RUN_TEST(test_sync_explicit_await_calls_with_keepalive);
  RUN_TEST(test_sync_implicit_await_calls_with_keepalive);
  RUN_TEST(test_parallel_explicit_await_calls);
  RUN_TEST(test_timeout_continue);

//  UNITY_END();
}

void loop()
{
  static int i = 0;
  RUN_TEST(test_parallel_explicit_await_calls);
  if (i >= 3)
    UNITY_END(); // stop unit testing
  i++;
}
