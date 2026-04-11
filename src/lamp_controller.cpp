// LampSmart Pro BLE lamp controller for ESP32
// Uses Arduino BLE library for reliable advertising
// Sends BOTH V1 and V3 encodings (like the phone app)

#include "lamp_controller.h"
#include "config.h"

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <mbedtls/aes.h>
#include <Arduino.h>

// --- V1 constants ---
static const uint8_t V1_HEADER[2] = {0x77, 0xF8};
static const uint8_t V1_PREFIX[8] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};

// --- V3 constants ---
static const uint8_t V3_HEADER[2] = {0xF0, 0x08};
static const uint8_t V3_PREFIX[3] = {0x30, 0x80, 0x00};
static const uint16_t V3_DEVICE_TYPE = 0x0100;

static const uint8_t XBOXES[128] = {
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC,
    0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A,
    0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85,
    0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9,
    0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94,
    0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68,
    0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

static bool ble_initialized = false;
static uint8_t tx_count = 0;
static BLEAdvertising *pAdvertising = nullptr;

// ============================================================
// Common
// ============================================================

static uint16_t crc16(const uint8_t *buf, size_t len, uint16_t seed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

static uint16_t swap16(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

// ============================================================
// V1 encoding
// ============================================================

static void v1_whiten(uint8_t *buf, size_t len, uint8_t seed) {
    uint8_t r = seed;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = 0;
        for (size_t j = 0; j < 8; j++) {
            r <<= 1;
            if (r & 0x80) { r ^= 0x11; b |= 1 << j; }
            r &= 0x7F;
        }
        buf[i] ^= b;
    }
}

static void v1_reverse_all(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t x = buf[i];
        x = ((x & 0x55) << 1) | ((x & 0xAA) >> 1);
        x = ((x & 0x33) << 2) | ((x & 0xCC) >> 2);
        x = ((x & 0x0F) << 4) | ((x & 0xF0) >> 4);
        buf[i] = x;
    }
}

typedef struct __attribute__((packed)) {
    uint8_t  command;
    uint16_t group_index;
    uint8_t  args[3];
    uint8_t  tx_count;
    uint8_t  outs;
    uint8_t  src;
    uint8_t  r2;
    uint16_t seed;
    uint16_t crc16;
} v1_data_map_t;

// Build 26-byte UUID data for V1 (header + 24 encoded bytes)
static void build_v1_uuid_data(uint8_t *out, uint8_t cmd, uint8_t arg0, uint8_t arg1, uint8_t arg2,
                                uint32_t id, uint8_t count) {
    uint8_t buf[24];
    memset(buf, 0, 24);
    memcpy(buf, V1_PREFIX, 8);

    v1_data_map_t *data = (v1_data_map_t *)(buf + 8);
    uint16_t seed = esp_random() & 0xFFF5;
    uint8_t seed8 = seed & 0xFF;

    data->command = cmd;
    data->group_index = (uint16_t)(id & 0xF0FF);
    data->args[0] = arg0;
    data->args[1] = arg1;
    data->args[2] = arg2;
    data->tx_count = count;
    data->outs = 0;
    data->src = seed8 ^ ((id >> 16) & 0xFF);
    data->r2 = seed8;
    data->seed = swap16(seed);
    data->crc16 = swap16(crc16((uint8_t *)data, sizeof(v1_data_map_t) - 2, ~seed));

    uint16_t crc_mac = crc16(buf + 1, 5, 0xFFFF);
    uint16_t crc2 = crc16((uint8_t *)data, sizeof(v1_data_map_t), crc_mac);
    buf[22] = (crc2 >> 8) & 0xFF;
    buf[23] = crc2 & 0xFF;

    v1_reverse_all(buf, 24);
    v1_whiten(buf, 24, 0x6F);

    // UUID data = header(2) + encoded(24) = 26 bytes
    out[0] = V1_HEADER[0];
    out[1] = V1_HEADER[1];
    memcpy(out + 2, buf, 24);
}

// ============================================================
// V3 encoding
// ============================================================

typedef struct __attribute__((packed)) {
    uint8_t  tx_count;
    uint16_t type;
    uint32_t identifier;
    uint8_t  group_index;
    uint16_t command;
    uint8_t  args[4];
    uint16_t sign;
    uint8_t  spare;
    uint16_t seed;
    uint16_t crc16;
} v3_data_map_t;

static void v3_whiten(uint8_t *buf, uint8_t size, uint8_t seed, uint8_t salt) {
    for (uint8_t i = 0; i < size; i++) {
        buf[i] ^= XBOXES[((seed + i + 9) & 0x1F) + (salt & 0x3) * 0x20];
        buf[i] ^= seed;
    }
}

static uint16_t v3_sign(uint8_t *buf, uint8_t tx_count, uint16_t seed) {
    uint8_t sigkey[16] = {0, 0, 0, 0x0D, 0xBF, 0xE6, 0x42, 0x68,
                          0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16};
    sigkey[0] = seed & 0xFF;
    sigkey[1] = (seed >> 8) & 0xFF;
    sigkey[2] = tx_count;

    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    mbedtls_aes_setkey_enc(&aes_ctx, sigkey, 128);

    uint8_t aes_in[16], aes_out[16];
    memcpy(aes_in, buf, 16);
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, aes_in, aes_out);
    mbedtls_aes_free(&aes_ctx);

    uint16_t s = ((uint16_t *)aes_out)[0];
    return s == 0 ? 0xFFFF : s;
}

// Build 26-byte UUID data for V3 (header + 24 encoded bytes)
static void build_v3_uuid_data_4args(uint8_t *out, uint8_t cmd, uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3,
                                      uint32_t id, uint8_t count) {
    uint8_t buf[24];
    memset(buf, 0, 24);

    memcpy(buf, V3_PREFIX, 3);

    v3_data_map_t *data = (v3_data_map_t *)(buf + 3);
    uint16_t seed = esp_random() & 0xFFFF;

    data->tx_count = count;
    data->type = V3_DEVICE_TYPE;
    data->identifier = id;
    data->group_index = (uint8_t)(id & 0xFF);
    data->command = (uint16_t)cmd;
    data->args[0] = a0;
    data->args[1] = a1;
    data->args[2] = a2;
    data->args[3] = a3;
    data->sign = 0;
    data->spare = 0;
    data->seed = seed;
    data->crc16 = 0;

    // AES signature
    data->sign = v3_sign(buf + 1, count, seed);

    // Whiten buf[2..19]
    v3_whiten(buf + 2, 18, (uint8_t)seed, 0);

    // CRC of first 22 bytes
    data->crc16 = crc16(buf, 22, ~seed);

    // UUID data = header(2) + encoded(24) = 26 bytes
    out[0] = V3_HEADER[0];
    out[1] = V3_HEADER[1];
    memcpy(out + 2, buf, 24);
}

// ============================================================
// BLE advertising via Arduino BLE library
// ============================================================

static void advertise_packet(uint8_t *uuid_data_26) {
    BLEAdvertisementData advData;

    // Build raw AD data: Flags(3) + UUID list(28) = 31 bytes
    char raw[31];
    raw[0] = 0x02;  // Flags length
    raw[1] = 0x01;  // Flags type
    raw[2] = 0x02;  // LE General Discoverable
    raw[3] = 0x1B;  // UUID list length (27)
    raw[4] = 0x03;  // Complete 16-bit UUID list
    memcpy(raw + 5, uuid_data_26, 26);

    advData.addData(std::string(raw, 31));

    pAdvertising->stop();
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();
    delay(BLE_ADV_DURATION_MS);
    pAdvertising->stop();
}

// ============================================================
// Public API
// ============================================================

void LampController::begin() {
    if (ble_initialized) return;

    BLEDevice::init("");
    Serial.printf("[BLE] MAC: %s\n", BLEDevice::getAddress().toString().c_str());

    pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->setMinInterval(0x20);
    pAdvertising->setMaxInterval(0x20);
    // Non-connectable, non-scannable
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    tx_count = esp_random() & 0xFF;
    ble_initialized = true;
    Serial.println("[BLE] ready");
}

void LampController::pair() {
    Serial.printf("[LAMP] pair: ID=0x%06X (V1+V3)\n", CONTROLLER_ID);
    uint8_t v1_arg0 = CONTROLLER_ID & 0xFF;
    uint8_t v1_arg1 = (CONTROLLER_ID >> 8) & 0xF0;
    sendCommand(0x28, v1_arg0, v1_arg1, 0x81, BLE_PAIR_REPEAT);
}

void LampController::unpair() { sendCommand(0x45); }
void LampController::on()     { sendCommand(0x10); }
void LampController::off()    { sendCommand(0x11); }
void LampController::setBrightness(uint8_t cold, uint8_t warm) {
    // V3: WCOLOR args shifted to [0, 0, cold, warm]
    for (int i = 0; i < BLE_ADV_REPEAT; i++) {
        uint8_t uuid_data[26];
        build_v3_uuid_data_4args(uuid_data, 0x21, 0, 0, cold, warm, CONTROLLER_ID, tx_count);
        advertise_packet(uuid_data);
        tx_count++;
    }
    Serial.printf("[WCOLOR] cold=%d warm=%d done\n", cold, warm);
}
void LampController::setRGB(uint8_t r, uint8_t g, uint8_t b) {
    // V3 args mapping: shift to args[1..3] (args[0]=0)
    // Temporarily override sendCommand to put RGB in right positions
    sendRGBCommand(r, g, b);
}
void LampController::nightLight() { sendCommand(0x12); }

void LampController::sendCommand(uint8_t cmd, uint8_t arg0, uint8_t arg1, uint8_t arg2, int repeat) {
    for (int i = 0; i < repeat; i++) {
        uint8_t uuid_data[26];

        // V3 only
        build_v3_uuid_data_4args(uuid_data, cmd, arg0, arg1, arg2, 0, CONTROLLER_ID, tx_count);
        advertise_packet(uuid_data);

        tx_count++;
        if (i % 10 == 0) Serial.printf("[I] %d/%d\n", i + 1, repeat);
    }
    Serial.println("[LAMP] done");
}

void LampController::sendRGBCommand(uint8_t r, uint8_t g, uint8_t b) {
    // Try args mapping: [0, R, G, B] (shifted like WCOLOR in ESPHome V2)
    for (int i = 0; i < BLE_ADV_REPEAT; i++) {
        uint8_t uuid_data[26];
        build_v3_uuid_data_4args(uuid_data, 0x22, 0, r, g, b, CONTROLLER_ID, tx_count);
        if (i == 0) {
            Serial.printf("[RGB] args=[0,%d,%d,%d]\n", r, g, b);
        }
        advertise_packet(uuid_data);
        tx_count++;
    }
    Serial.println("[RGB] done");
}
