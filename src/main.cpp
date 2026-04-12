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

    // アクション種別
    enum Action { NONE, LAMP_PAIR, LAMP_ON, LAMP_OFF, LAMP_RGB, LAMP_BRIGHTNESS, LAMP_NIGHT,
                  MONITOR_ON, MONITOR_OFF, TAPE_ON, TAPE_OFF, DESK1, DESK2 };
    Action action = NONE;
    uint8_t r = 0, g = 0, b = 0, cold = 0, warm = 0;

    if (req.indexOf("POST /pair") >= 0) {
        action = LAMP_PAIR;
        response += "{\"action\":\"pair\"}";
    } else if (req.indexOf("POST /on") >= 0) {
        action = LAMP_ON;
        response += "{\"action\":\"on\"}";
    } else if (req.indexOf("POST /off") >= 0) {
        action = LAMP_OFF;
        response += "{\"action\":\"off\"}";
    } else if (req.indexOf("POST /rgb") >= 0) {
        int ri = req.indexOf("r="); if (ri > 0) r = req.substring(ri+2).toInt();
        int gi = req.indexOf("g="); if (gi > 0) g = req.substring(gi+2).toInt();
        int bi = req.indexOf("b="); if (bi > 0) b = req.substring(bi+2).toInt();
        action = LAMP_RGB;
        response += "{\"action\":\"rgb\",\"r\":" + String(r) + ",\"g\":" + String(g) + ",\"b\":" + String(b) + "}";
    } else if (req.indexOf("POST /brightness") >= 0) {
        int ci = req.indexOf("cold="); if (ci > 0) cold = req.substring(ci+5).toInt();
        int wi = req.indexOf("warm="); if (wi > 0) warm = req.substring(wi+5).toInt();
        action = LAMP_BRIGHTNESS;
        response += "{\"action\":\"brightness\",\"cold\":" + String(cold) + ",\"warm\":" + String(warm) + "}";
    } else if (req.indexOf("POST /night") >= 0) {
        action = LAMP_NIGHT;
        response += "{\"action\":\"night\"}";
    } else if (req.indexOf("POST /monitor/on") >= 0) {
        action = MONITOR_ON;
        response += "{\"action\":\"monitor\",\"state\":\"on\"}";
    } else if (req.indexOf("POST /monitor/off") >= 0) {
        action = MONITOR_OFF;
        response += "{\"action\":\"monitor\",\"state\":\"off\"}";
    } else if (req.indexOf("POST /tape/on") >= 0) {
        action = TAPE_ON;
        response += "{\"action\":\"tape\",\"state\":\"on\"}";
    } else if (req.indexOf("POST /tape/off") >= 0) {
        action = TAPE_OFF;
        response += "{\"action\":\"tape\",\"state\":\"off\"}";
    } else if (req.indexOf("POST /desk/1") >= 0) {
        action = DESK1;
        response += "{\"action\":\"desk\",\"button\":1}";
    } else if (req.indexOf("POST /desk/2") >= 0) {
        action = DESK2;
        response += "{\"action\":\"desk\",\"button\":2}";
    } else {
        response += "{\"status\":\"ok\",\"device\":\"bt-light\"}";
    }

    // レスポンスを先に返す
    client.print(response);
    delay(1);
    client.stop();

    // アクション実行（BLEは時間がかかるためレスポンス後に処理）
    switch (action) {
        case LAMP_PAIR:       lamp.pair(); break;
        case LAMP_ON:         lamp.on(); break;
        case LAMP_OFF:        lamp.off(); break;
        case LAMP_RGB:        lamp.setRGB(r, g, b); break;
        case LAMP_BRIGHTNESS: lamp.setBrightness(cold, warm); break;
        case LAMP_NIGHT:      lamp.nightLight(); break;
        case MONITOR_ON:      digitalWrite(RELAY_MONITOR_PIN, HIGH); break;
        case MONITOR_OFF:     digitalWrite(RELAY_MONITOR_PIN, LOW); break;
        case TAPE_ON:         digitalWrite(RELAY_TAPE_PIN, HIGH); break;
        case TAPE_OFF:        digitalWrite(RELAY_TAPE_PIN, LOW); break;
        case DESK1:
            digitalWrite(DESK_BTN1_PIN, HIGH);
            delay(DESK_PULSE_MS);
            digitalWrite(DESK_BTN1_PIN, LOW);
            break;
        case DESK2:
            digitalWrite(DESK_BTN2_PIN, HIGH);
            delay(DESK_PULSE_MS);
            digitalWrite(DESK_BTN2_PIN, LOW);
            break;
        default: break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== bt-light ===");
    // リレーGPIO初期化（HIGH=ON）
    pinMode(RELAY_TAPE_PIN, OUTPUT);
    pinMode(RELAY_MONITOR_PIN, OUTPUT);
    digitalWrite(RELAY_TAPE_PIN, HIGH);
    digitalWrite(RELAY_MONITOR_PIN, HIGH);
    Serial.printf("[RELAY] tape=GPIO%d, monitor=GPIO%d\n", RELAY_TAPE_PIN, RELAY_MONITOR_PIN);

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
