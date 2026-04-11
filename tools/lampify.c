// FanLamp Pro BLE lamp controller
// Based on analysis of LampSmart Pro phone HCI snoop logs and
// aronsky/esphome-components FanLampEncoderV1
//
// Protocol: lampsmart_pro v1
//   - Header: [0x77, 0xF8] (constant, not encoded)
//   - Encoding: reverse_all + LFSR whiten (seed 0x6F)
//   - 24-byte encoded buffer: prefix(8) + data_map(14) + crc2(2)
//   - AD type 0x03 (16-bit Service UUIDs), Flags 0x01

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

// --- Protocol constants ---
static const uint8_t HEADER[2] = {0x77, 0xF8};
static const uint8_t PREFIX[8] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};

// Default controller ID (24-bit)
static uint32_t CONTROLLER_ID = 0x000001;

// --- FanLampV1 data map (packed, 14 bytes) ---
typedef struct __attribute__((packed)) {
    uint8_t  command;       // [0]
    uint16_t group_index;   // [1-2] LE
    uint8_t  args[3];       // [3-5]
    uint8_t  tx_count;      // [6]
    uint8_t  outs;          // [7]
    uint8_t  src;           // [8]
    uint8_t  r2;            // [9]
    uint16_t seed;          // [10-11] BE
    uint16_t crc16;         // [12-13] BE
} data_map_t;

// --- Utility functions ---

void whiten(uint8_t *buf, size_t len, uint8_t seed) {
    uint8_t r = seed;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = 0;
        for (size_t j = 0; j < 8; j++) {
            r <<= 1;
            if (r & 0x80) {
                r ^= 0x11;
                b |= 1 << j;
            }
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

// CRC16-CCITT big-endian (poly 0x1021)
uint16_t crc16be(uint8_t *buf, size_t len, uint16_t seed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

uint16_t htons_val(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

void print_hex(const char *label, uint8_t *buf, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
}

// --- Build lampsmart_pro V1 packet ---
// Builds 24-byte plaintext, encodes, and creates 31-byte AD packet.
// Returns AD data length (always 31).

int build_packet(uint8_t *adv, uint8_t cmd, uint8_t arg0, uint8_t arg1, uint8_t arg2,
                 uint32_t id, uint8_t tx_count) {
    uint8_t buf[24];
    memset(buf, 0, 24);

    // Prefix (8 bytes)
    memcpy(buf, PREFIX, 8);

    // Data map (14 bytes at offset 8)
    data_map_t *data = (data_map_t *)(buf + 8);

    uint16_t seed = rand() & 0xFFF5;
    uint8_t seed8 = seed & 0xFF;

    data->command = cmd;
    data->group_index = (uint16_t)(id & 0xF0FF);
    data->args[0] = arg0;
    data->args[1] = arg1;
    data->args[2] = arg2;  // pair_arg3 for PAIR, 0 for others
    data->tx_count = tx_count;
    data->outs = 0;
    data->src = seed8 ^ ((id >> 16) & 0xFF);
    data->r2 = seed8;
    data->seed = htons_val(seed);
    data->crc16 = htons_val(crc16be((uint8_t *)data, sizeof(data_map_t) - 2, ~seed));

    // CRC2: additional CRC over data_map using MAC-derived seed
    // crc_mac = CRC16 of prefix[1:6] with seed 0xFFFF
    uint16_t crc_mac = crc16be(buf + 1, 5, 0xFFFF);
    // crc2 = CRC16 of data_map (14 bytes) with seed crc_mac
    uint16_t crc2 = crc16be((uint8_t *)data, sizeof(data_map_t), crc_mac);
    buf[22] = (crc2 >> 8) & 0xFF;  // BE
    buf[23] = crc2 & 0xFF;

    // Encode: bit-reverse then whiten with seed 0x6F
    reverse_all(buf, 24);
    whiten(buf, 24, 0x6F);

    // Build AD packet (31 bytes)
    memset(adv, 0, 31);
    adv[0] = 0x02;  // AD length
    adv[1] = 0x01;  // AD type: Flags
    adv[2] = 0x01;  // Flags: LE Limited Discoverable (matches phone)
    adv[3] = 0x1B;  // AD length: 27
    adv[4] = 0x03;  // AD type: 16-bit Service UUIDs (complete)
    // Header (constant, not encoded)
    adv[5] = HEADER[0];  // 0x77
    adv[6] = HEADER[1];  // 0xF8
    // Encoded data (24 bytes)
    memcpy(adv + 7, buf, 24);

    return 31;
}

// --- HCI functions ---

int hciSetParams(int sock) {
    unsigned char status;
    struct hci_request req;
    le_set_advertising_parameters_cp params;
    memset(&req, 0, sizeof(req));
    memset(&params, 0, sizeof(params));
    params.min_interval = 0x00A0;  // 100ms (match phone)
    params.max_interval = 0x00D2;  // 131ms (match phone)
    params.advtype = 0;  // ADV_IND
    params.chan_map = 7;  // All channels
    req.ogf = 0x08;
    req.ocf = 0x0006;
    req.cparam = &params;
    req.rparam = &status;
    req.clen = sizeof(params);
    req.rlen = 1;
    if (hci_send_req(sock, &req, 1000) < 0 || status) return -1;
    return 0;
}

int sendOnePacket(int sock, uint8_t *adv_data, int adv_len) {
    uint8_t hci_buf[32];
    memset(hci_buf, 0, 32);
    hci_buf[0] = adv_len;
    memcpy(hci_buf + 1, adv_data, adv_len);

    if (hci_send_cmd(sock, 0x08, 0x0008, 32, hci_buf) < 0) {
        fprintf(stderr, "[E] Failed to set advertising data!\n");
        return -1;
    }
    if (hci_le_set_advertise_enable(sock, 0x01, 1000) < 0) {
        fprintf(stderr, "[E] Failed to start advertising!\n");
        return -1;
    }
    usleep(2000000);  // 2s per advertisement (match phone's 2000ms duration)
    if (hci_le_set_advertise_enable(sock, 0x00, 1000) < 0) {
        fprintf(stderr, "[E] Failed to stop advertising!\n");
        return -1;
    }
    return 0;
}

// --- Main ---

int main(int argc, char **argv) {
    srand(time(NULL));

    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <pair|on|off|brightness|night> [controller_id_hex]\n"
            "  pair       - pair with light (power cycle first, send within 5s)\n"
            "  on         - turn on\n"
            "  off        - turn off\n"
            "  brightness - set brightness: cold,warm (0-255 each)\n"
            "  night      - night light mode\n"
            "  controller_id defaults to 000001\n", argv[0]);
        return 1;
    }

    char *command = argv[1];
    if (argc >= 3) {
        CONTROLLER_ID = strtoul(argv[2], NULL, 16);
    }

    // Command translation (FanLamp protocol)
    uint8_t cmd = 0;
    uint8_t arg0 = 0, arg1 = 0, arg2 = 0;
    int num_repeats = 8;

    if (!strcmp(command, "pair")) {
        cmd = 0x28;
        arg0 = CONTROLLER_ID & 0xFF;
        arg1 = (CONTROLLER_ID >> 8) & 0xF0;
        arg2 = 0x81;  // pair_arg3 (lampsmart_pro)
        num_repeats = 15;
    } else if (!strcmp(command, "on")) {
        cmd = 0x10;
    } else if (!strcmp(command, "off")) {
        cmd = 0x11;
    } else if (!strcmp(command, "brightness")) {
        cmd = 0x21;
        if (argc >= 4) {
            char *comma = strchr(argv[3], ',');
            if (comma) {
                arg0 = atoi(argv[3]);
                arg1 = atoi(comma + 1);
            } else {
                arg0 = atoi(argv[3]);
                arg1 = arg0;
            }
        } else {
            arg0 = 128;
            arg1 = 128;
        }
    } else if (!strcmp(command, "night")) {
        cmd = 0x12;
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }

    printf("[I] Command: %s (0x%02X), ID: 0x%06X, repeats: %d\n",
           command, cmd, CONTROLLER_ID, num_repeats);

    // Open HCI device
    int deviceID = hci_get_route(NULL);
    if (deviceID < 0) { fprintf(stderr, "[E] No adapter!\n"); return 1; }
    int sock = hci_open_dev(deviceID);
    if (sock < 0) { fprintf(stderr, "[E] Can't open socket!\n"); return 1; }
    if (hciSetParams(sock) < 0) {
        fprintf(stderr, "[E] Failed to set advertising params!\n");
        hci_close_dev(sock);
        return 1;
    }

    uint8_t tx_count = rand() & 0xFF;

    // Show first packet for debugging
    {
        uint8_t adv[31];
        build_packet(adv, cmd, arg0, arg1, arg2, CONTROLLER_ID, tx_count);
        print_hex("[D] Full ADV packet", adv, 31);
        print_hex("[D] Header", adv + 5, 2);
        print_hex("[D] Encoded data", adv + 7, 24);
    }

    for (int i = 0; i < num_repeats; i++) {
        uint8_t adv[31];
        build_packet(adv, cmd, arg0, arg1, arg2, CONTROLLER_ID, tx_count);

        if (sendOnePacket(sock, adv, 31) < 0) break;

        tx_count++;
        printf("[I] Sent packet %d/%d\n", i + 1, num_repeats);
    }

    hci_close_dev(sock);
    printf("[I] Done.\n");
    return 0;
}
