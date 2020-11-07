#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

ESP8266WiFiMulti WiFiMulti; // ?
WiFiClient client;
HTTPClient http;

// variables for server commands

// ip-address?
// server-given bot ID?

//

void setup() {
    // start
    Serial.begin(115200);

    // connect to server
    WiFi.mode(WIFI_AP_STA); // both AP and STA
    WiFi.begin("bot", "dankmemes");
    Serial.print("\nConnecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\nConnected, IP address: ");
    Serial.println(WiFi.localIP()); // we send this to the server

    // register with server
    http.begin(client, "http://192.168.1.186:42/register")
    int httpCode = http.POST("{}");
    Serial.println(httpCode);
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println(response);
        // do something
        // maybe we return a timer for when to check back for localization step?

    }
    http.end();

    // do localization

    // receive server commands for loop start
}

void loop() {
    // take sensor measurements

    // connect to WiFi

    //   send data to server

    //   receive next motor / sensor commands

    // pipe motor commands to motor controller



    // wait for WiFi connection
    if ((WiFiMulti.run() == WL_CONNECTED)) {
        Serial.print("[HTTP] begin...\n");
        if (http.begin(client, "http://jigsaw.w3.org/HTTP/connection.html")) {  // HTTP
            Serial.print("[HTTP] GET...\n");
            // start connection and send HTTP header
            int httpCode = http.GET();

            // httpCode will be negative on error
            if (httpCode > 0) {
                // HTTP header has been send and Server response header has been handled
                Serial.printf("[HTTP] GET... code: %d\n", httpCode);

                // file found at server
                if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }
            } else {
                Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
            }

            http.end();
        } else {
            Serial.printf("[HTTP} Unable to connect\n");
        }
    }

    delay(10000);
}
