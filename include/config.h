#pragma once

// WiFi設定 - credentials.h があればそちらを使用
#if __has_include("credentials.h")
#include "credentials.h"
#else
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"
#endif

// HTTP APIポート
#define HTTP_PORT 80

// ライトのコントローラID（24ビット）
// スマホの既存ペアリングID: 0x0950CF
#define CONTROLLER_ID 0x0950CF

// trueにしてアップロード → ライトの電源入れ直し → ESP32リセット
// BLE初期化直後（WiFi前）にpairを送る
#define PAIR_ON_BOOT false

// BLEアドバタイジング送信時間(ms)
#define BLE_ADV_DURATION_MS 200

// BLEアドバタイジング繰り返し回数
#define BLE_ADV_REPEAT 30

// ペアリング時の繰り返し回数（ウィンドウが短いので多めに）
#define BLE_PAIR_REPEAT 50

// リレー制御GPIOピン
#define RELAY_MONITOR_PIN 25   // モニターライト
#define RELAY_RF_PIN      26   // RFライト

// デスク制御GPIOピン（S8050経由、パルスでボタン押下）
#define DESK_BTN1_PIN 12   // メモリーボタン1
#define DESK_BTN2_PIN 13   // メモリーボタン2
#define DESK_PULSE_MS 300  // ボタン押下時間(ms)
