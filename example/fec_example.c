/*
 * STeRG S1.0 FEC example for STM32.
 *
 * Demonstrates the two UHF chains defined in FEC-FINAL-002:
 *
 *   UHF HK   (VCID 0) : RS(255,223) + Conv 1/2 K=7   (CCSDS 131.0-B-5)
 *   UHF Beacon (VCID 7): RS(255,223) shortened       (CCSDS 131.0-B-5 §4)
 *
 * The function returns 0 on success. Wire it up in main() to blink an LED
 * after HAL init. The decoders need ~20-25 KB of heap — bump
 * _Min_Heap_Size in the linker script to 0x8000.
 */
#include <string.h>
#include <stdint.h>

#include "sterg_fec.h"

/* ---------- UHF HK ---------- */

static int hk_example(void) {
    uint8_t msg[STERG_HK_MSG_LEN];
    uint8_t encoded[STERG_HK_ENCODED_MAX_LEN];
    uint8_t decoded[STERG_HK_MSG_LEN];

    for (size_t i = 0; i < STERG_HK_MSG_LEN; i++) {
        msg[i] = (uint8_t)(i * 37u + 11u);
    }

    sterg_uhf_hk_t *hk = sterg_uhf_hk_create();
    if (!hk) return 1;

    size_t bits = sterg_uhf_hk_encode(hk, msg, encoded);
    if (bits == 0) { sterg_uhf_hk_destroy(hk); return 2; }

    /* Simulated channel noise — Viterbi should scrub a few bit-flips out */
    encoded[12] ^= 0x10;
    encoded[77] ^= 0x02;
    encoded[200] ^= 0x40;

    if (sterg_uhf_hk_decode(hk, encoded, bits, decoded) != 0) {
        sterg_uhf_hk_destroy(hk); return 3;
    }
    if (memcmp(msg, decoded, STERG_HK_MSG_LEN) != 0) {
        sterg_uhf_hk_destroy(hk); return 4;
    }

    sterg_uhf_hk_destroy(hk);
    return 0;
}

/* ---------- UHF Beacon ---------- */

static int beacon_example(void) {
    /* Exercise all five beacon modes with a small burst of byte errors
     * well within the RS correction capability (up to 16 per block). */
    static const sterg_beacon_mode_t modes[] = {
        STERG_BEACON_MODE_NOMINAL,
        STERG_BEACON_MODE_PRELINK,
        STERG_BEACON_MODE_DETUMBLING,
        STERG_BEACON_MODE_LOWPOWER,
        STERG_BEACON_MODE_CRITICAL,
    };

    sterg_uhf_beacon_t *bcn = sterg_uhf_beacon_create();
    if (!bcn) return 10;

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        sterg_beacon_mode_t mode = modes[m];
        size_t payload_len = sterg_beacon_payload_len(mode);

        uint8_t payload[STERG_BEACON_MAX_PAYLOAD];
        uint8_t encoded[STERG_BEACON_MAX_ENCODED];
        uint8_t decoded[STERG_BEACON_MAX_PAYLOAD];

        for (size_t i = 0; i < payload_len; i++) {
            payload[i] = (uint8_t)((i * 53u + mode) ^ 0x5A);
        }

        size_t enc_len = sterg_uhf_beacon_encode(bcn, mode, payload, encoded);
        if (enc_len != payload_len + STERG_BEACON_PARITY) {
            sterg_uhf_beacon_destroy(bcn); return 20 + (int)mode;
        }

        /* Flip up to 8 bytes — well under the 16-byte capacity */
        for (int k = 0; k < 8; k++) {
            encoded[(k * 7) % enc_len] ^= 0xA5;
        }

        ssize_t dec_len = sterg_uhf_beacon_decode(bcn, mode, encoded, decoded);
        if (dec_len != (ssize_t)payload_len) {
            sterg_uhf_beacon_destroy(bcn); return 30 + (int)mode;
        }
        if (memcmp(payload, decoded, payload_len) != 0) {
            sterg_uhf_beacon_destroy(bcn); return 40 + (int)mode;
        }
    }

    sterg_uhf_beacon_destroy(bcn);
    return 0;
}

/* Returns 0 on full success. Nonzero return encodes which subtest failed. */
int fec_sterg_example_run(void) {
    int rc;

    rc = hk_example();
    if (rc) return rc;

    rc = beacon_example();
    if (rc) return rc;

    return 0;
}
