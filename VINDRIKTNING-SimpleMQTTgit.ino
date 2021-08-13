#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
constexpr static const uint8_t PIN_UART_RX = 4; // D2 on Wemos D1 Mini
constexpr static const uint8_t PIN_UART_TX = 13; // UNUSED
SoftwareSerial sensorSerial(PIN_UART_RX, PIN_UART_TX);
uint8_t serialRxBuf[255];
uint8_t rxBufIdx = 0;
struct particleSensorState_t {
    uint16_t avgPM25 = 0;
    uint16_t measurements[5] = {0, 0, 0, 0, 0};
    uint8_t measurementIdx = 0;
    boolean valid = false;
};
particleSensorState_t state;
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
String avg = "";
const char* SSID = "YOURSSID";
const char* PSK = "YOURPASSWORD";
const char* MQTT_BROKER = "YOURBROKER";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* OTApass = "OTAPASS";
const char* ClientName = "PM25Sensor";
const char* Topic = "/home/data/pm25/1";
const char* QualityTopic = "/home/data/pm25/1/quality";

void setup() {
 sensorSerial.begin(9600);
 Serial.begin(115200);
     setup_wifi();
    client.setServer(MQTT_BROKER, 1883);
//    client.setCallback(callback);
    ArduinoOTA.setHostname(ClientName);
    ArduinoOTA.setPassword(OTApass);
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void clearRxBuf() {
  memset(serialRxBuf, 0, sizeof(serialRxBuf));
  rxBufIdx = 0;
}

void parseState(particleSensorState_t& state) {
   const uint16_t pm25 = (serialRxBuf[5] << 8) | serialRxBuf[6];
   Serial.printf("Received PM 2.5 reading: %d\n", pm25);
   state.measurements[state.measurementIdx] = pm25;
   state.measurementIdx = (state.measurementIdx + 1) % 5;
     if (state.measurementIdx == 0) {
         float avgPM25 = 0.0f;
         for (uint8_t i = 0; i < 5; ++i) {
             avgPM25 += state.measurements[i] / 5.0f;
         }
         state.avgPM25 = avgPM25;
         state.valid = true;
         Serial.printf("New Avg PM25: %d\n", state.avgPM25);
         client.publish(Topic, String(state.avgPM25).c_str(), true);
         if (state.avgPM25 < 30) {
          client.publish(QualityTopic, "excellent");
         }
         if (state.avgPM25 > 30 && state.avgPM25 < 100 ) {
          client.publish(QualityTopic, "good");
         }
         if (state.avgPM25 > 100 && state.avgPM25 < 1000 ) {
          client.publish(QualityTopic, "poor");
         }
     }
     clearRxBuf();
}
bool isValidHeader() {
        bool headerValid = serialRxBuf[0] == 0x16 && serialRxBuf[1] == 0x11 && serialRxBuf[2] == 0x0B;

        if (!headerValid) {
            Serial.println("Received message with invalid header.");
        }

        return headerValid;
    }

    bool isValidChecksum() {
        uint8_t checksum = 0;

        for (uint8_t i = 0; i < 20; i++) {
            checksum += serialRxBuf[i];
        }

        if (checksum != 0) {
            Serial.printf("Received message with invalid checksum. Expected: 0. Actual: %d\n", checksum);
        }

        return checksum == 0;
    }

    void handleUart(particleSensorState_t& state) {
        if (!sensorSerial.available()) {
            return;
        }

        Serial.print("Receiving:");
        while (sensorSerial.available()) {
            serialRxBuf[rxBufIdx++] = sensorSerial.read();
            Serial.print(".");

            // Without this delay, receiving data breaks for reasons that are beyond me
            delay(15);

            if (rxBufIdx >= 64) {
                clearRxBuf();
            }
        }
        Serial.println("Done.");

        if (isValidHeader() && isValidChecksum()) {
            parseState(state);

            Serial.printf(
                "Current measurements: %d, %d, %d, %d, %d\n",

                state.measurements[0],
                state.measurements[1],
                state.measurements[2],
                state.measurements[3],
                state.measurements[4]
            );
        } else {
            clearRxBuf();
        }
    }
    void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(SSID);
 
    WiFi.begin(SSID, PSK);
 
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
 
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}
void reconnect() {
    while (!client.connected()) {
        Serial.println("Reconnecting MQTT...");
        if (!client.connect(ClientName, mqttUser, mqttPassword)) { 
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds");
            delay(5000);
        }
    }
    Serial.println("MQTT Connected...");
}

void loop() {
  if (!client.connected()) {
      reconnect();
  }
  ArduinoOTA.handle();
  client.loop();
  handleUart(state);
}
