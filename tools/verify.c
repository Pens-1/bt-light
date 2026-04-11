// Verify our encoder output matches the phone's lampsmart_pro V1 protocol
// Decodes generated packets and checks structure against phone's HCI snoop log

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Same functions as lampify.c
void whiten(uint8_t *buf, size_t len, uint8_t seed) {
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

void reverse_all(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t x = buf[i];
        x = ((x & 0x55) << 1) | ((x & 0xAA) >> 1);
        x = ((x & 0x33) << 2) | ((x & 0xCC) >> 2);
        x = ((x & 0x0F) << 4) | ((x & 0xF0) >> 4);
        buf[i] = x;
    }
}

uint16_t crc16be(uint8_t *buf, size_t len, uint16_t seed) {
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

uint16_t htons_val(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
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
} data_map_t;

void print_hex(const char *label, uint8_t *buf, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
}

// Decode a 24-byte encoded buffer (after header [77 F8])
void decode_v1(uint8_t *encoded, int label) {
    uint8_t buf[24];
    memcpy(buf, encoded, 24);

    // Decode: un-whiten(0x6F) then un-reverse
    whiten(buf, 24, 0x6F);
    reverse_all(buf, 24);

    uint8_t prefix[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};

    printf("=== Packet %d decoded ===\n", label);
    print_hex("  Plaintext", buf, 24);
    printf("  Prefix: %s\n", memcmp(buf, prefix, 8) == 0 ? "OK" : "MISMATCH");

    data_map_t *data = (data_map_t *)(buf + 8);
    uint16_t seed = htons_val(data->seed);
    uint8_t seed8 = seed & 0xFF;

    const char *cmd_names[] = {"???", "ON", "OFF", "NIGHT", "BRIGHTNESS", "PAIR"};
    int cmd_idx = 0;
    if (data->command == 0x10) cmd_idx = 1;
    else if (data->command == 0x11) cmd_idx = 2;
    else if (data->command == 0x12) cmd_idx = 3;
    else if (data->command == 0x21) cmd_idx = 4;
    else if (data->command == 0x28) cmd_idx = 5;

    printf("  Command: %s (0x%02X)\n", cmd_names[cmd_idx], data->command);
    printf("  Group index: 0x%04X\n", data->group_index);
    printf("  Args: [%02X, %02X, %02X]\n", data->args[0], data->args[1], data->args[2]);
    printf("  TX count: %d\n", data->tx_count);
    printf("  Seed: 0x%04X (seed8=0x%02X)\n", seed, seed8);
    printf("  r2: 0x%02X %s\n", data->r2, (data->r2 == seed8) ? "OK" : "MISMATCH");
    printf("  src: 0x%02X (id_upper = 0x%02X)\n", data->src, data->src ^ seed8);

    // Verify CRC
    uint16_t expected_crc = crc16be((uint8_t *)data, sizeof(data_map_t) - 2, ~seed);
    uint16_t actual_crc = htons_val(data->crc16);
    printf("  CRC: expected=0x%04X actual=0x%04X %s\n",
           expected_crc, actual_crc, (expected_crc == actual_crc) ? "OK" : "MISMATCH");

    // Verify CRC2
    uint16_t crc_mac = crc16be(buf + 1, 5, 0xFFFF);
    uint16_t expected_crc2 = crc16be((uint8_t *)data, sizeof(data_map_t), crc_mac);
    uint16_t actual_crc2 = ((uint16_t)buf[22] << 8) | buf[23];
    printf("  CRC2: expected=0x%04X actual=0x%04X %s\n",
           expected_crc2, actual_crc2, (expected_crc2 == actual_crc2) ? "OK" : "MISMATCH");

    // Reconstruct full ID
    uint32_t id = (uint32_t)(data->src ^ seed8) << 16 | data->group_index;
    printf("  Full ID: 0x%06X\n\n", id);
}

int main() {
    srand(time(NULL));

    // --- Test 1: Generate a packet and verify round-trip ---
    printf("=== Test 1: Generate and verify our packet ===\n\n");

    uint32_t test_id = 0x000001;
    uint8_t buf[24];
    memset(buf, 0, 24);
    memcpy(buf, (uint8_t[]){0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46}, 8);

    data_map_t *data = (data_map_t *)(buf + 8);
    uint16_t seed = rand() & 0xFFF5;
    uint8_t seed8 = seed & 0xFF;

    data->command = 0x28;
    data->group_index = (uint16_t)(test_id & 0xF0FF);
    data->args[0] = test_id & 0xFF;
    data->args[1] = (test_id >> 8) & 0xF0;
    data->args[2] = 0x81;
    data->tx_count = 42;
    data->outs = 0;
    data->src = seed8 ^ ((test_id >> 16) & 0xFF);
    data->r2 = seed8;
    data->seed = htons_val(seed);
    data->crc16 = htons_val(crc16be((uint8_t *)data, sizeof(data_map_t) - 2, ~seed));

    // CRC2
    uint16_t crc_mac = crc16be(buf + 1, 5, 0xFFFF);
    uint16_t crc2 = crc16be((uint8_t *)data, sizeof(data_map_t), crc_mac);
    buf[22] = (crc2 >> 8) & 0xFF;
    buf[23] = crc2 & 0xFF;

    print_hex("  Plaintext", buf, 24);

    // Encode
    reverse_all(buf, 24);
    whiten(buf, 24, 0x6F);
    print_hex("  Encoded", buf, 24);

    // Decode and verify
    decode_v1(buf, 1);

    // --- Test 2: Verify phone's PAIR packet (from HCI snoop log) ---
    printf("=== Test 2: Decode phone V1 PAIR packet ===\n\n");
    // UUID data: 77 F8 B6 5F 2B 5E 00 FC 31 51 CC 49 92 FB 2E 4A BB FC 8E CC F4 3A 2F A7 84 B0
    // Header: [77 F8] (skip), Encoded: 24 bytes after header
    uint8_t phone_pair[] = {
        0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51,
        0xCC, 0x49, 0x92, 0xFB, 0x2E, 0x4A, 0xBB, 0xFC,
        0x8E, 0xCC, 0xF4, 0x3A, 0x2F, 0xA7, 0x84, 0xB0
    };
    decode_v1(phone_pair, 2);

    // --- Test 3: Verify phone's ON packet ---
    printf("=== Test 3: Decode phone V1 ON packet ===\n\n");
    uint8_t phone_on[] = {
        0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51,
        0xD0, 0x49, 0x92, 0x08, 0x24, 0xCB, 0x1B, 0xFC,
        0x21, 0x63, 0xF4, 0x95, 0x5E, 0x9A, 0xAD, 0xF6
    };
    decode_v1(phone_on, 3);

    // --- Test 4: Verify phone's OFF packet ---
    printf("=== Test 4: Decode phone V1 OFF packet ===\n\n");
    uint8_t phone_off[] = {
        0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51,
        0x50, 0x49, 0x92, 0x08, 0x24, 0xCB, 0xFB, 0xFC,
        0x7A, 0x38, 0xF4, 0xCE, 0xC4, 0x1B, 0xB3, 0xEC
    };
    decode_v1(phone_off, 4);

    return 0;
}
