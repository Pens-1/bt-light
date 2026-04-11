// Decode phone BLE advertising packets to identify the protocol
// Tries FanLampV1 (esphome) and original lampify decoding

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// --- LFSR whiten (same as esphome BleAdvEncoder::whiten) ---
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

// Original lampify whitening (with 13-byte offset)
void lampify_whiten(uint8_t *input, uint8_t *output, uint8_t len) {
    uint8_t whArr[38];
    memset(whArr, 0, 13);
    for (int i = 0; i < len && i < 25; i++) {
        whArr[i + 13] = input[i];
    }
    int i2 = 83;  // 0x53
    for (int i3 = 0; i3 < 38; i3++) {
        int i4 = i2;
        uint8_t b = 0;
        for (int i5 = 0; i5 < 8; i5++) {
            int i6 = i4 & 0xFF;
            b |= ((((i6 & 64) >> 6) << i5) ^ (whArr[i3] & 0xFF)) & (1 << i5);
            int i7 = i6 << 1;
            int i8 = (i7 >> 7) & 1;
            int i9 = (i7 & ~1) | i8;
            i4 = ((i9 ^ (i8 << 4)) & 16) | (i9 & ~16);
        }
        whArr[i3] = b;
        i2 = i4;
    }
    for (int i = 0; i < len && i < 25; i++) {
        output[i] = whArr[i + 13];
    }
}

void print_hex(const char *label, uint8_t *buf, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

// Phone Set B first packet (raw bytes from UUIDs, little-endian)
// UUIDs: 08f9 1349 69f0 4e25 5131 aeba 02ba c106 3dba 0771 553b 2aa7 4ce3
// Raw bytes (LE): F9 08 49 13 F0 69 25 4E 31 51 BA AE BA 02 06 C1 BA 3D 71 07 3B 55 A7 2A E3 4C

int main() {
    // Phone packets (Set B - first 3 unique patterns)
    uint8_t setB[3][26] = {
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x3D, 0x71, 0x07, 0x3B, 0x55, 0xA7, 0x2A, 0xE3, 0x4C},
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0xDD, 0x71, 0xD3, 0x3B, 0xEF, 0x73, 0x64, 0xE3, 0xE7},  // txcount+1?
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x5D, 0x71, 0x10, 0x3B, 0x2C, 0xB0, 0x16, 0xE3, 0x4C},
    };

    uint8_t setA[3][26] = {
        {0xF0, 0x08, 0x20, 0x82, 0xFD, 0x7E, 0x93, 0x5C, 0xEA, 0xF9, 0x8D, 0x95, 0xD8, 0xDF, 0x08, 0x52, 0xB2, 0xEA, 0x71, 0xD9, 0xF4, 0x69, 0x3F, 0x57, 0x74, 0xAB},
        {0xF0, 0x08, 0x20, 0x82, 0xFD, 0x7F, 0x93, 0x5C, 0xEA, 0xF9, 0x8D, 0x95, 0xD8, 0xDF, 0x08, 0x52, 0xB2, 0xEA, 0x71, 0xA5, 0xF4, 0x69, 0x3F, 0x57, 0x02, 0x9E},
        {0xF0, 0x08, 0x20, 0x82, 0xFD, 0x78, 0x93, 0x5C, 0xEA, 0xF9, 0x8D, 0x95, 0xD8, 0xDF, 0x08, 0x52, 0xB2, 0xEA, 0x71, 0x6E, 0xF4, 0x69, 0x3F, 0x57, 0x1A, 0x8E},
    };

    uint8_t setC[3][26] = {
        {0x77, 0xF8, 0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51, 0xCC, 0x98, 0x92, 0x2A, 0x2E, 0x4A, 0xFA, 0xFC, 0x84, 0x3D, 0xF4, 0xCB, 0xC2, 0x3F, 0xAA, 0xFF},
        {0x77, 0xF8, 0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51, 0xCC, 0x98, 0x92, 0x2A, 0x2E, 0x4A, 0xFA, 0xFC, 0x84, 0x28, 0x91, 0xDE, 0xA2, 0x91, 0x63, 0xB6},
        {0x77, 0xF8, 0xB6, 0x5F, 0x2B, 0x5E, 0x00, 0xFC, 0x31, 0x51, 0xCC, 0x98, 0x92, 0x2A, 0x2E, 0x4A, 0xFA, 0xFC, 0x84, 0xE1, 0x58, 0x17, 0x2D, 0xD6, 0x55, 0x3B},
    };

    printf("=== Try esphome FanLampV1 decode (un-whiten 0x6F, un-reverse) ===\n\n");

    // Try decoding all 26 bytes as FanLampV1
    for (int set = 0; set < 3; set++) {
        uint8_t *data;
        const char *name;
        if (set == 0) { data = setB[0]; name = "Set B[0]"; }
        else if (set == 1) { data = setA[0]; name = "Set A[0]"; }
        else { data = setC[0]; name = "Set C[0]"; }

        printf("--- %s ---\n", name);
        print_hex("Raw", data, 26);

        // Try different header offsets (0, 1, 2, 3 bytes of header)
        for (int hdr = 0; hdr <= 3; hdr++) {
            uint8_t buf[26];
            memcpy(buf, data + hdr, 26 - hdr);
            int dlen = 26 - hdr;

            // un-whiten then un-reverse (reverse of encode order)
            whiten(buf, dlen, 0x6F);
            reverse_all(buf, dlen);

            printf("  hdr=%d, un-whiten(0x6F)+un-reverse: ", hdr);
            for (int i = 0; i < dlen && i < 16; i++) printf("%02X ", buf[i]);
            printf("...\n");

            // Check for known prefix
            uint8_t prefix[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
            for (int off = 0; off <= dlen - 8; off++) {
                if (memcmp(buf + off, prefix, 8) == 0) {
                    printf("  *** MATCH: prefix found at offset %d! ***\n", off);
                }
            }
        }
        printf("\n");
    }

    printf("\n=== Try original lampify decode (un-lampify_whiten, un-reverse) ===\n\n");

    for (int set = 0; set < 3; set++) {
        uint8_t *data;
        const char *name;
        if (set == 0) { data = setB[0]; name = "Set B[0]"; }
        else if (set == 1) { data = setA[0]; name = "Set A[0]"; }
        else { data = setC[0]; name = "Set C[0]"; }

        printf("--- %s ---\n", name);

        // Try treating first 25 bytes as the lampify-encoded data
        // (last byte would be trailing 0x00)
        uint8_t reversed[25];
        memcpy(reversed, data, 25);

        // un-lampify-whiten
        uint8_t unwhitened[25];
        // lampify_whiten is its own inverse (XOR-based)
        lampify_whiten(reversed, unwhitened, 25);

        // un-reverse
        reverse_all(unwhitened, 25);

        print_hex("  un-lampify+un-rev (25B)", unwhitened, 25);

        // Check for known prefix/preamble
        uint8_t preamble[] = {0x71, 0x0F, 0x55};
        if (memcmp(unwhitened, preamble, 3) == 0) {
            printf("  *** PREAMBLE MATCH at offset 0! ***\n");
        }
        uint8_t prefix[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
        if (memcmp(unwhitened + 3, prefix, 8) == 0) {
            printf("  *** PREFIX MATCH at offset 3! ***\n");
        }
        printf("\n");
    }

    printf("\n=== Try different whiten seeds for full 26 bytes ===\n");
    // Try all possible seeds with reverse_all + whiten, looking for the prefix
    uint8_t *data = setB[0];
    uint8_t prefix[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};

    for (int seed = 0; seed < 128; seed++) {
        for (int hdr = 0; hdr <= 3; hdr++) {
            uint8_t buf[26];
            int dlen = 26 - hdr;
            memcpy(buf, data + hdr, dlen);
            whiten(buf, dlen, seed);
            reverse_all(buf, dlen);

            for (int off = 0; off <= dlen - 8; off++) {
                if (memcmp(buf + off, prefix, 8) == 0) {
                    printf("  SEED=0x%02X, hdr=%d: prefix at offset %d: ", seed, hdr, off);
                    print_hex("decoded", buf, dlen);
                }
            }
        }
    }

    printf("\n=== XOR analysis: find the whiten mask by XORing with known plaintext ===\n");
    // If we assume Set B starts with some header then the prefix [AA 98 43 AF 0B 46 46 46]
    // We can XOR the encoded bytes with the expected plaintext to find the whiten mask
    // For header=0 (no header), data starts at byte 0, prefix at byte 0:
    // For header=2, data starts at byte 2, prefix at byte 0 of encoded data:

    // In original lampify:
    // plaintext[0..2] = PREAMBLE {0x71, 0x0F, 0x55}
    // plaintext[3..10] = prefix {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46}
    // After bit-reverse:
    uint8_t known_plain[11] = {0x71, 0x0F, 0x55, 0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
    uint8_t known_reversed[11];
    memcpy(known_reversed, known_plain, 11);
    reverse_all(known_reversed, 11);
    printf("Known plaintext after reverse: ");
    for (int i = 0; i < 11; i++) printf("%02X ", known_reversed[i]);
    printf("\n");

    // XOR with Set B raw bytes to get the whiten mask
    printf("XOR mask (SetB[0] ^ reversed_plaintext): ");
    for (int i = 0; i < 11; i++) {
        printf("%02X ", setB[0][i] ^ known_reversed[i]);
    }
    printf("\n");

    // Now generate whiten masks for various seeds and check
    printf("\nSearching for matching whiten seed (lampify-style with 13-byte offset)...\n");
    for (int seed = 0; seed < 128; seed++) {
        // Generate LFSR output for 38 bytes starting from seed
        uint8_t lfsr_out[38];
        memset(lfsr_out, 0, 38);
        uint8_t r = seed;
        for (int i = 0; i < 38; i++) {
            uint8_t b = 0;
            for (int j = 0; j < 8; j++) {
                r <<= 1;
                if (r & 0x80) {
                    r ^= 0x11;
                    b |= 1 << j;
                }
                r &= 0x7F;
            }
            lfsr_out[i] = b;
        }
        // Check if XOR mask at offset 13 matches
        int match = 1;
        for (int i = 0; i < 11; i++) {
            if (lfsr_out[i + 13] != (setB[0][i] ^ known_reversed[i])) {
                match = 0;
                break;
            }
        }
        if (match) {
            printf("  *** MATCH: seed=0x%02X (lampify offset 13) ***\n", seed);
            // Decode full packet
            uint8_t decoded[25];
            memcpy(decoded, setB[0], 25);
            for (int i = 0; i < 25; i++) decoded[i] ^= lfsr_out[i + 13];
            reverse_all(decoded, 25);
            print_hex("  Full decoded", decoded, 25);
        }

        // Also check without offset (esphome style)
        match = 1;
        for (int i = 0; i < 11; i++) {
            if (lfsr_out[i] != (setB[0][i] ^ known_reversed[i])) {
                match = 0;
                break;
            }
        }
        if (match) {
            printf("  *** MATCH: seed=0x%02X (no offset) ***\n", seed);
        }
    }

    // Also try for Set A and Set C
    printf("\n=== Set A XOR analysis ===\n");
    printf("XOR mask (SetA[0] ^ reversed_plaintext): ");
    for (int i = 0; i < 11; i++) {
        printf("%02X ", setA[0][i] ^ known_reversed[i]);
    }
    printf("\n");

    printf("\n=== Set C XOR analysis ===\n");
    printf("XOR mask (SetC[0] ^ reversed_plaintext): ");
    for (int i = 0; i < 11; i++) {
        printf("%02X ", setC[0][i] ^ known_reversed[i]);
    }
    printf("\n");

    return 0;
}
