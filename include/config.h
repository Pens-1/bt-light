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
