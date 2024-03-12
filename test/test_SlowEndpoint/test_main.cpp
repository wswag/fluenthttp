#include <Arduino.h>
#include <unity.h>
#include <fluenthttp.h>
#include <env.h>

//#define WIFI_CLIENT

#ifdef WIFI_CLIENT
#include <WiFi.h>
WiFiClient client;
#else
#include <Ethernet.h>
#include <EthernetClient.h>
#include <utility/w5100.h>
EthernetClient client;
#endif

#define ENDPOINT_IP IPAddress(192,168,178,41)

ServiceEndpoint endpoint(ENDPOINT_IP);

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
  while (cursor < r.contentReader->available()) {    
    auto nextChunk = min(r.contentReader->available(), (int)sizeof(buffer)-1);
    memset(buffer, 0, sizeof(buffer));
    int i = r.contentReader->readBytes(buffer, nextChunk);
    cursor += i;
    printf(buffer);
  }
  printf("\r\n");
}

void get_request(int timeout, bool sync) {
  printf("connect to server...\r\n");
  auto t0 = millis();
  ServiceRequest& request = endpoint.get("/status");
  auto t1 = millis();
  bool success = false;
  bool* successPtr = &success;
  printf("write GET request...\r\n");
  
  request.withTimeout(timeout)
      .onSuccess([=](service_response_t r) {
          *successPtr = true;
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
    if (sync) {
      auto t2 = millis();
      request.await();
      auto t3 = millis();
      printf("timing specs: open tcp %d msec, write data %d msec, return %d msec\r\n\r\n", t1-t0, t2-t1, t3-t2);
      //TEST_ASSERT_TRUE(success);
    }
}

void test_sync_explicit_await_calls_with_close() {
  endpoint.withKeepAlive(false);
    for (int k = 0; k <= 10; k++) {
        delay(3000);
        get_request(10000, true);
        //endpoint.close();
    }
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
  client.setTimeout(10);
  #else
  log_d("initializing ethernet on cs pin %d", ETHERNET_SPI_CS_PIN);
  Ethernet.init(ETHERNET_SPI_CS_PIN);
  uint8_t mac[6] = ETHERNET_MAC_ADDRESS;
  if (!Ethernet.begin(mac)) {
    log_e("eth connection failed");
  }
  printf("eth chip: %d\r\n", W5100.getChip());
  Ethernet.setRetransmissionTimeout(50);
  Ethernet.setRetransmissionCount(200);
  client.setConnectionTimeout(10000);
  client.setTimeout(10000);
  #endif
  
  endpoint.begin(&client);

//  UNITY_END();
}

void loop()
{
  static int i = 0;
  RUN_TEST(test_sync_explicit_await_calls_with_close);
  if (i >= 3)
    UNITY_END(); // stop unit testing
  i++;
}
