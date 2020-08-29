#include "Arduino.h"
#include "WiFi.h"
#include "thingspeak.h"
#include "watchdog.h"
#include "debugUtils.h"
#include "time.h"

#include "pass.h"

int sensor_pin = 34;
int sensor_pin2 = 35;
RTC_DATA_ATTR unsigned long bootCount = 0;
const char* ntpServer = "pool.ntp.org";
const char * HOST     = "192.168.113.244";    // push gateway IP or host
const uint16_t PORT   = 9091;   //push gateway port

void setup() {
  setupWatchdog(60);
  DEBUG_SERIAL_START(115200);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN();
  DEBUG_PRINTLN();
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  bootCount = bootCount + 1;
}

unsigned long getTime() {
  configTime(0, 0, ntpServer);
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    DEBUG_PRINTLN("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

String getPrometheusString(unsigned long ts, int v1, int v2, unsigned long wifiConnectDuration) {
  return "# TYPE timestamp gauge"
      "\ntimestamp " + String(ts) +
      "\n#TYPE sensor1 gauge" +
      "\nsensor1 " + String(v1) +
      "\n#TYPE sensor2 gauge" +
      "\nsensor2 " + String(v2) +
      "\n#TYPE wifiConnectDuration gauge" +
      "\nwifiConnectDuration " + String(wifiConnectDuration) +
      "\n#TYPE bootCount counter" +
      "\nbootCount " + String(bootCount) +
      "\n";
}

void sendToPrometheus(String data) {
  WiFiClient client;

  if (!client.connect(HOST, PORT)) {
    DEBUG_PRINTLN("Connection to push gate failed.");
    return;
  }

  client.print("POST /metrics/job/floodingSensor/instance/esp32_stoupacka HTTP/1.1\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print("Content-Length: "); client.print(data.length()); client.print("\r\n");
  client.print("Host: "); client.print(HOST); client.print(":"); client.print(PORT); client.print("\r\n");
  client.print("\r\n");
  client.print(data);

  int maxloops = 0;
  // wait for the server's reply to become available
  while (!client.available() && maxloops < 1000) {
    maxloops++;
    delay(1); //delay 1 msec
  }
  if (client.available() > 0) {
    while (client.available() > 0) {
      //read back one line from the server
      String line = client.readStringUntil('\r');
      DEBUG_PRINT("> ");
      DEBUG_PRINTLN(line);
    }
  } else {
    DEBUG_PRINTLN("HTTP Response timeout from server");
  }

  DEBUG_PRINTLN("Closing connection.");
  client.stop();
}

void loop() {
  delay(2000); //give some time to external sensors to "boot up" - they need some time when they are powered from ESP32
  feedWatchdog();
  DEBUG_PRINTLN("reading moisture");
  int value= analogRead(sensor_pin);
  value = map(value,4096,0,0,100);
  int value2 = analogRead(sensor_pin2);
  value2 = map(value2,4096,0,0,100);
  DEBUG_PRINT("Moisture : ");
  DEBUG_PRINT(value);
  DEBUG_PRINT(" / ");
  DEBUG_PRINTLN(value2);

  DEBUG_PRINTLN("connecting to wifi");

  unsigned long wifiConnectStartedAt = millis();
  WiFi.begin(WifiSsid, WifiPassword);

  while (WiFi.status() != WL_CONNECTED) {   //TODO zblbuvzdornit pripojovani na wifi. Kdyz se nepodari po x vterinach, zrestartovat
    delay(500);
    DEBUG_PRINT(".");
  }
  unsigned long wifiConnectFinishedAt = millis();

  feedWatchdog();
  DEBUG_PRINTLN("IP address: ");
  DEBUG_PRINTLN(WiFi.localIP());

  DEBUG_PRINT("Current timestamp: ");
  unsigned long currentTime = getTime();
  DEBUG_PRINTLN(currentTime);

  String prom = getPrometheusString(currentTime, value, value2, wifiConnectFinishedAt - wifiConnectStartedAt);

  DEBUG_PRINTLN(prom);

  sendToPrometheus(prom);


  DEBUG_PRINTLN("Going to sleep, night night!");
  unsigned long sleepTime = 3600000000L; //jak dlouho chci spat: 60 minut
  sleepTime = 30 * 1000 * 1000;
  esp_sleep_enable_timer_wakeup(sleepTime);
  esp_deep_sleep_start();
}