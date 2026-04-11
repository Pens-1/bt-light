// Send raw BLE advertising data directly
// Used to test if the phone's exact packets work from our dongle

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <time.h>

int main() {
    // Phone's actual Set B pair packets (from btmon capture)
    // These are the 26 bytes of UUID data from the phone's advertising
    uint8_t phone_packets[][26] = {
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x3D, 0x71, 0x07, 0x3B, 0x55, 0xA7, 0x2A, 0xE3, 0x4C},
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0xDD, 0x71, 0xD3, 0x3B, 0xEF, 0x73, 0x64, 0xE3, 0xE7},
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x5D, 0x71, 0x10, 0x3B, 0x2C, 0xB0, 0x16, 0xE3, 0x4C},
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x9D, 0x71, 0x7C, 0x3B, 0x40, 0xDC, 0x41, 0xE3, 0x81},
        {0xF9, 0x08, 0x49, 0x13, 0xF0, 0x69, 0x25, 0x4E, 0x31, 0x51, 0xBA, 0xAE, 0xBA, 0x02, 0x06, 0xC1, 0xBA, 0x1D, 0x71, 0xDD, 0x3B, 0xE1, 0x7D, 0xED, 0xE3, 0xFE},
    };
    int num_packets = 5;

    // Build full AD packets
    uint8_t adv[31];
    adv[0] = 0x02;  // Flags length
    adv[1] = 0x01;  // Flags type
    adv[2] = 0x1A;  // Flags value (same as phone)
    adv[3] = 0x1B;  // UUID list length (27)
    adv[4] = 0x03;  // UUID list type

    // Open HCI
    int deviceID = hci_get_route(NULL);
    if (deviceID < 0) { fprintf(stderr, "No adapter!\n"); return 1; }
    int sock = hci_open_dev(deviceID);
    if (sock < 0) { fprintf(stderr, "Can't open socket!\n"); return 1; }

    // Set advertising params
    unsigned char status;
    struct hci_request req;
    le_set_advertising_parameters_cp params;
    memset(&req, 0, sizeof(req));
    memset(&params, 0, sizeof(params));
    params.min_interval = 0x0020;
    params.max_interval = 0x0020;
    params.chan_map = 7;
    req.ogf = 0x08;
    req.ocf = 0x0006;
    req.cparam = &params;
    req.rparam = &status;
    req.clen = sizeof(params);
    req.rlen = 1;
    if (hci_send_req(sock, &req, 1000) < 0) {
        fprintf(stderr, "Failed to set params!\n");
        hci_close_dev(sock);
        return 1;
    }

    printf("Sending phone's EXACT pair packets for 30 seconds...\n");
    printf("(Power cycle the light first, then run this within 5 seconds)\n\n");

    int rounds = 0;
    time_t start = time(NULL);

    while (time(NULL) - start < 30) {
        for (int p = 0; p < num_packets; p++) {
            memcpy(adv + 5, phone_packets[p], 26);

            // Set advertising data
            uint8_t hci_buf[32];
            memset(hci_buf, 0, 32);
            hci_buf[0] = 31;
            memcpy(hci_buf + 1, adv, 31);

            if (hci_send_cmd(sock, 0x08, 0x0008, 32, hci_buf) < 0) {
                fprintf(stderr, "Failed to set adv data!\n");
                goto done;
            }
            if (hci_le_set_advertise_enable(sock, 0x01, 1000) < 0) {
                fprintf(stderr, "Failed to start adv!\n");
                goto done;
            }
            usleep(100000);
            hci_le_set_advertise_enable(sock, 0x00, 1000);
        }
        rounds++;
        if (rounds % 5 == 0) {
            printf("%d rounds (%ds)...\n", rounds, (int)(time(NULL) - start));
        }
    }

done:
    hci_close_dev(sock);
    printf("Done. %d rounds.\n", rounds);
    return 0;
}
