/*
 * STeRG S1.0 FEC verification harness (see FEC_FINAL_DECISION.pdf).
 *
 * Tested configurations:
 *
 *   UHF HK     RS(255,223) + Conv 1/2 K=7    CCSDS 131.0-B-5
 *   UHF Beacon RS(255,223) shortened, 5 modes  CCSDS 131.0-B-5 §4
 *
 * Also spot-checks the low-level correct_* API with CCSDS RS parameters
 * (primitive 0x187, first root 112, root gap 11) to catch regressions
 * that don't show up through the sterg_fec wrappers.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "correct.h"
#include "sterg_fec.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("[%02d] %-60s ", tests_run, name); \
    fflush(stdout); \
} while (0)

#define PASS() do { printf("PASS\n"); } while (0)

#define FAIL(fmt, ...) do { \
    tests_failed++; \
    printf("FAIL  " fmt "\n", ##__VA_ARGS__); \
} while (0)

static void fill_pattern(uint8_t *buf, size_t n, unsigned int seed) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)((i * 37u + seed) ^ (i >> 3));
    }
}

/* ===================================================================
 * Low-level CCSDS RS(255,223) sanity checks
 * =================================================================== */

static void ccsds_rs_clean_roundtrip(void) {
    TEST("CCSDS RS(255,223) clean round-trip");
    uint8_t msg[223], enc[255], dec[223];
    fill_pattern(msg, 223, 1);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 112, 11, 32);
    if (!rs) { FAIL("create"); return; }

    correct_reed_solomon_encode(rs, msg, 223, enc);
    ssize_t n = correct_reed_solomon_decode(rs, enc, 255, dec);
    correct_reed_solomon_destroy(rs);

    if (n != 223 || memcmp(msg, dec, 223)) { FAIL("mismatch (n=%zd)", n); return; }
    PASS();
}

static void ccsds_rs_corrects_16(void) {
    TEST("CCSDS RS(255,223) corrects 16 byte errors");
    uint8_t msg[223], enc[255], dec[223];
    fill_pattern(msg, 223, 2);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 112, 11, 32);
    if (!rs) { FAIL("create"); return; }

    correct_reed_solomon_encode(rs, msg, 223, enc);
    for (int i = 0; i < 16; i++) enc[i * 15] ^= 0x5A;

    ssize_t n = correct_reed_solomon_decode(rs, enc, 255, dec);
    correct_reed_solomon_destroy(rs);

    if (n != 223 || memcmp(msg, dec, 223)) { FAIL("mismatch (n=%zd)", n); return; }
    PASS();
}

/* ===================================================================
 * UHF HK — RS + Conv chain
 * =================================================================== */

static void hk_clean(void) {
    TEST("UHF HK clean round-trip (223 B -> 4096 bits -> 223 B)");
    uint8_t msg[STERG_HK_MSG_LEN];
    uint8_t enc[STERG_HK_ENCODED_MAX_LEN];
    uint8_t dec[STERG_HK_MSG_LEN];
    fill_pattern(msg, STERG_HK_MSG_LEN, 3);

    sterg_uhf_hk_t *hk = sterg_uhf_hk_create();
    if (!hk) { FAIL("create"); return; }

    size_t bits = sterg_uhf_hk_encode(hk, msg, enc);
    if (bits != 4096) { FAIL("encode returned %zu bits", bits); goto out; }

    if (sterg_uhf_hk_decode(hk, enc, bits, dec) != 0) { FAIL("decode"); goto out; }
    if (memcmp(msg, dec, STERG_HK_MSG_LEN)) { FAIL("payload mismatch"); goto out; }
    PASS();
out:
    sterg_uhf_hk_destroy(hk);
}

static void hk_with_bit_flips(int num_flips, const char *label) {
    char name[96];
    snprintf(name, sizeof(name), "UHF HK recovers %d scattered bit flips (%s)",
             num_flips, label);
    TEST(name);

    uint8_t msg[STERG_HK_MSG_LEN];
    uint8_t enc[STERG_HK_ENCODED_MAX_LEN];
    uint8_t dec[STERG_HK_MSG_LEN];
    fill_pattern(msg, STERG_HK_MSG_LEN, 5);

    sterg_uhf_hk_t *hk = sterg_uhf_hk_create();
    if (!hk) { FAIL("create"); return; }

    size_t bits = sterg_uhf_hk_encode(hk, msg, enc);
    if (bits == 0) { FAIL("encode"); goto out; }
    size_t bytes = bits / 8;

    /* Sprinkle single-bit flips across the encoded stream */
    for (int i = 0; i < num_flips; i++) {
        size_t byte_idx = (i * 211u + 17u) % bytes;
        uint8_t bit = (uint8_t)(1u << ((i * 3u + 1u) & 7u));
        enc[byte_idx] ^= bit;
    }

    if (sterg_uhf_hk_decode(hk, enc, bits, dec) != 0) {
        FAIL("decode"); goto out;
    }
    if (memcmp(msg, dec, STERG_HK_MSG_LEN)) { FAIL("payload mismatch"); goto out; }
    PASS();
out:
    sterg_uhf_hk_destroy(hk);
}

/* ===================================================================
 * UHF Beacon — shortened RS, all 5 modes
 * =================================================================== */

static const char *beacon_mode_name(sterg_beacon_mode_t m) {
    switch (m) {
        case STERG_BEACON_MODE_NOMINAL:    return "Nominal";
        case STERG_BEACON_MODE_PRELINK:    return "Pre-link";
        case STERG_BEACON_MODE_DETUMBLING: return "Detumbling";
        case STERG_BEACON_MODE_LOWPOWER:   return "Low power";
        case STERG_BEACON_MODE_CRITICAL:   return "Critical";
    }
    return "?";
}

static void beacon_mode(sterg_beacon_mode_t mode, int flips) {
    char name[96];
    size_t payload_len = sterg_beacon_payload_len(mode);
    snprintf(name, sizeof(name),
             "UHF Beacon %s (mode %d, %zu B) corrects %d byte errors",
             beacon_mode_name(mode), (int)mode, payload_len, flips);
    TEST(name);

    sterg_uhf_beacon_t *bcn = sterg_uhf_beacon_create();
    if (!bcn) { FAIL("create"); return; }

    uint8_t payload[STERG_BEACON_MAX_PAYLOAD];
    uint8_t enc[STERG_BEACON_MAX_ENCODED];
    uint8_t dec[STERG_BEACON_MAX_PAYLOAD];
    fill_pattern(payload, payload_len, 31 + (unsigned)mode);

    size_t enc_len = sterg_uhf_beacon_encode(bcn, mode, payload, enc);
    if (enc_len != payload_len + 32) { FAIL("encode len %zu", enc_len); goto out; }

    for (int i = 0; i < flips; i++) {
        enc[(i * 7u) % enc_len] ^= 0xA5;
    }

    ssize_t n = sterg_uhf_beacon_decode(bcn, mode, enc, dec);
    if (n != (ssize_t)payload_len) { FAIL("decode n=%zd", n); goto out; }
    if (memcmp(payload, dec, payload_len)) { FAIL("payload mismatch"); goto out; }
    PASS();
out:
    sterg_uhf_beacon_destroy(bcn);
}

static void beacon_invalid_mode(void) {
    TEST("UHF Beacon rejects invalid mode");
    sterg_uhf_beacon_t *bcn = sterg_uhf_beacon_create();
    if (!bcn) { FAIL("create"); return; }

    if (sterg_beacon_payload_len((sterg_beacon_mode_t)99) != 0) {
        FAIL("payload_len accepted bogus mode");
    } else {
        PASS();
    }
    sterg_uhf_beacon_destroy(bcn);
}

/* ===================================================================
 * Main
 * =================================================================== */

int main(void) {
    printf("STeRG S1.0 FEC verification harness\n");
    printf("===================================\n");

    ccsds_rs_clean_roundtrip();
    ccsds_rs_corrects_16();

    hk_clean();
    hk_with_bit_flips(5,  "below Viterbi capacity");
    hk_with_bit_flips(20, "moderate burst");

    beacon_mode(STERG_BEACON_MODE_NOMINAL,    8);
    beacon_mode(STERG_BEACON_MODE_PRELINK,    8);
    beacon_mode(STERG_BEACON_MODE_DETUMBLING, 8);
    beacon_mode(STERG_BEACON_MODE_LOWPOWER,   8);
    beacon_mode(STERG_BEACON_MODE_CRITICAL,  16); /* at the 16-byte RS limit */
    beacon_invalid_mode();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
