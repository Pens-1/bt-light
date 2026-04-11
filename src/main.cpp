#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>

#include "config.h"
#include "lamp_controller.h"

LampController lamp;

// Minimal HTTP server (no external libs needed)
#include <WiFiServer.h>
WiFiServer httpServer(HTTP_PORT);

void setupWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] connecting to %s", WIFI_SSID);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[WiFi] FAILED!");
        return;
    }
    Serial.printf("\n[WiFi] connected: %s\n", WiFi.localIP().toString().c_str());
    httpServer.begin();
}

void handleHTTP() {
    WiFiClient client = httpServer.available();
    if (!client) return;

    String req = client.readStringUntil('\r');
    client.flush();

    String response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";

    if (req.indexOf("POST /pair") >= 0) {
        lamp.pair();
        response += "{\"action\":\"pair\"}";
    } else if (req.indexOf("POST /on") >= 0) {
        lamp.on();
        response += "{\"action\":\"on\"}";
    } else if (req.indexOf("POST /off") >= 0) {
        lamp.off();
        response += "{\"action\":\"off\"}";
    } else if (req.indexOf("POST /rgb") >= 0) {
        // Parse ?r=255&g=0&b=128
        uint8_t r = 0, g = 0, b = 0;
        int ri = req.indexOf("r="); if (ri > 0) r = req.substring(ri+2).toInt();
        int gi = req.indexOf("g="); if (gi > 0) g = req.substring(gi+2).toInt();
        int bi = req.indexOf("b="); if (bi > 0) b = req.substring(bi+2).toInt();
        lamp.setRGB(r, g, b);
        response += "{\"action\":\"rgb\",\"r\":" + String(r) + ",\"g\":" + String(g) + ",\"b\":" + String(b) + "}";
    } else if (req.indexOf("POST /brightness") >= 0) {
        uint8_t cold = 0, warm = 0;
        int ci = req.indexOf("cold="); if (ci > 0) cold = req.substring(ci+5).toInt();
        int wi = req.indexOf("warm="); if (wi > 0) warm = req.substring(wi+5).toInt();
        lamp.setBrightness(cold, warm);
        response += "{\"action\":\"brightness\",\"cold\":" + String(cold) + ",\"warm\":" + String(warm) + "}";
    } else if (req.indexOf("POST /night") >= 0) {
        lamp.nightLight();
        response += "{\"action\":\"night\"}";
    } else if (req.indexOf("POST /monitor/on") >= 0) {
        digitalWrite(RELAY_MONITOR_PIN, HIGH);
        response += "{\"action\":\"monitor\",\"state\":\"on\"}";
    } else if (req.indexOf("POST /monitor/off") >= 0) {
        digitalWrite(RELAY_MONITOR_PIN, LOW);
        response += "{\"action\":\"monitor\",\"state\":\"off\"}";
    } else if (req.indexOf("POST /rflight/on") >= 0) {
        digitalWrite(RELAY_RF_PIN, HIGH);
        response += "{\"action\":\"rflight\",\"state\":\"on\"}";
    } else if (req.indexOf("POST /rflight/off") >= 0) {
        digitalWrite(RELAY_RF_PIN, LOW);
        response += "{\"action\":\"rflight\",\"state\":\"off\"}";
    } else if (req.indexOf("POST /desk/1") >= 0) {
        digitalWrite(DESK_BTN1_PIN, HIGH);
        delay(DESK_PULSE_MS);
        digitalWrite(DESK_BTN1_PIN, LOW);
        response += "{\"action\":\"desk\",\"button\":1}";
    } else if (req.indexOf("POST /desk/2") >= 0) {
        digitalWrite(DESK_BTN2_PIN, HIGH);
        delay(DESK_PULSE_MS);
        digitalWrite(DESK_BTN2_PIN, LOW);
        response += "{\"action\":\"desk\",\"button\":2}";
    } else {
        response += "{\"status\":\"ok\",\"device\":\"bt-light\"}";
    }

    client.print(response);
    delay(1);
    client.stop();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== bt-light ===");
    // リレーGPIO初期化（LOW=OFF）
    pinMode(RELAY_MONITOR_PIN, OUTPUT);
    pinMode(RELAY_RF_PIN, OUTPUT);
    digitalWrite(RELAY_MONITOR_PIN, LOW);
    digitalWrite(RELAY_RF_PIN, LOW);
    Serial.printf("[RELAY] monitor=GPIO%d, rf=GPIO%d\n", RELAY_MONITOR_PIN, RELAY_RF_PIN);

    // デスク制御GPIO初期化（LOW=OFF）
    pinMode(DESK_BTN1_PIN, OUTPUT);
    pinMode(DESK_BTN2_PIN, OUTPUT);
    digitalWrite(DESK_BTN1_PIN, LOW);
    digitalWrite(DESK_BTN2_PIN, LOW);
    Serial.printf("[DESK] btn1=GPIO%d, btn2=GPIO%d\n", DESK_BTN1_PIN, DESK_BTN2_PIN);

    lamp.begin();
    if (PAIR_ON_BOOT) {
        Serial.println("[BOOT] PAIR_ON_BOOT!");
        Serial.println("[BOOT] Waiting 3s...");
        delay(3000);
        Serial.println("[BOOT] Sending pair...");
        lamp.pair();
        Serial.println("[BOOT] pair done.");
    }
    setupWiFi();
}

void loop() {
    handleHTTP();
    delay(10);
}
