#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include <env.h>

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
  // clean stuff up here
}

uint8_t i = 0;
const uint8_t max_blinks = CONFIG_LWIP_MAX_SOCKETS;
WiFiClient* clients[max_blinks];

void test_client_connect() {
    // arrange
    auto c = new WiFiClient();
    int result = 0;

    // act
    result = c->connect(TEST_ENDPOINT, 1883);
    clients[i] = c;
    
    // assert
    TEST_ASSERT(result);
}

void test_all_connected() {
  for (int k = 0; k <= i; k++) {
    printf("testing client %d\r\n", k+1);
    TEST_ASSERT(clients[k]->connected());
  }
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
    delay(100);
  }
}

void loop()
{
  if (i < max_blinks)
  {
    RUN_TEST(test_client_connect);
    RUN_TEST(test_all_connected);
    delay(200);
    i++;
  }
  else if (i == max_blinks)
  {
    UNITY_END(); // stop unit testing
  }
}
