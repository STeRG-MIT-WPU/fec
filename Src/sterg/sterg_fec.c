/*
 * sterg_fec.c — STeRG S1.0 UHF FEC chains for STM32.
 *
 * See FEC_FINAL_DECISION.pdf §4 (UHF HK) and §5 (UHF Beacon).
 */
#include <stdlib.h>
#include <string.h>

#include "sterg_fec.h"

/* --- CCSDS 131.0-B-5 Reed-Solomon(255,223) parameters ----------------
 *  Primitive polynomial: x^8 + x^7 + x^2 + x + 1  (0x187)
 *  Generator polynomial: g(x) = prod_{j=112..143} (x - alpha^(11*j))
 *  →  first_consecutive_root = 112
 *  →  generator_root_gap     = 11
 *  →  num_roots              = 32  (t = 16)
 */
#define STERG_RS_PRIMITIVE_POLY   correct_rs_primitive_polynomial_ccsds
#define STERG_RS_FIRST_ROOT       112
#define STERG_RS_ROOT_GAP         11
#define STERG_RS_NUM_ROOTS        32


/* =====================================================================
 * UHF HK chain
 * ===================================================================== */

struct sterg_uhf_hk {
    correct_reed_solomon *rs;
    correct_convolutional *conv;

    /* scratch so encode/decode don't blow the caller's stack */
    uint8_t rs_block[STERG_HK_RS_BLOCK_LEN];
};

sterg_uhf_hk_t *sterg_uhf_hk_create(void) {
    sterg_uhf_hk_t *h = calloc(1, sizeof(*h));
    if (!h) return NULL;

    h->rs = correct_reed_solomon_create(
        STERG_RS_PRIMITIVE_POLY,
        STERG_RS_FIRST_ROOT,
        STERG_RS_ROOT_GAP,
        STERG_RS_NUM_ROOTS);
    if (!h->rs) goto fail;

    h->conv = correct_convolutional_create(
        2 /* rate 1/2 */,
        7 /* K = 7   */,
        correct_conv_ccsds_r12_7_polynomial);
    if (!h->conv) goto fail;

    return h;

fail:
    sterg_uhf_hk_destroy(h);
    return NULL;
}

void sterg_uhf_hk_destroy(sterg_uhf_hk_t *h) {
    if (!h) return;
    if (h->rs)   correct_reed_solomon_destroy(h->rs);
    if (h->conv) correct_convolutional_destroy(h->conv);
    free(h);
}

size_t sterg_uhf_hk_encode(sterg_uhf_hk_t *h,
                           const uint8_t msg[STERG_HK_MSG_LEN],
                           uint8_t *encoded) {
    if (!h || !msg || !encoded) return 0;

    ssize_t rs_len = correct_reed_solomon_encode(
        h->rs, msg, STERG_HK_MSG_LEN, h->rs_block);
    if (rs_len != STERG_HK_RS_BLOCK_LEN) return 0;

    return correct_convolutional_encode(
        h->conv, h->rs_block, STERG_HK_RS_BLOCK_LEN, encoded);
}

int sterg_uhf_hk_decode(sterg_uhf_hk_t *h,
                        const uint8_t *encoded, size_t num_bits,
                        uint8_t msg[STERG_HK_MSG_LEN]) {
    if (!h || !encoded || !msg) return -1;

    ssize_t viterbi_len = correct_convolutional_decode(
        h->conv, encoded, num_bits, h->rs_block);
    if (viterbi_len < STERG_HK_RS_BLOCK_LEN) return -1;

    ssize_t rs_msg_len = correct_reed_solomon_decode(
        h->rs, h->rs_block, STERG_HK_RS_BLOCK_LEN, msg);
    if (rs_msg_len != STERG_HK_MSG_LEN) return -1;

    return 0;
}


/* =====================================================================
 * UHF Beacon chain
 * ===================================================================== */

struct sterg_uhf_beacon {
    correct_reed_solomon *rs;
};

/* Payload length table — matches FEC_FINAL_DECISION §5.5 */
static const size_t beacon_payload_lut[] = {
    [STERG_BEACON_MODE_NOMINAL]    = 99,
    [STERG_BEACON_MODE_PRELINK]    = 96,
    [STERG_BEACON_MODE_DETUMBLING] = 89,
    [STERG_BEACON_MODE_LOWPOWER]   = 95,
    [STERG_BEACON_MODE_CRITICAL]   = 89,
};
#define BEACON_MODE_COUNT \
    (sizeof(beacon_payload_lut) / sizeof(beacon_payload_lut[0]))

size_t sterg_beacon_payload_len(sterg_beacon_mode_t mode) {
    if ((unsigned)mode >= BEACON_MODE_COUNT) return 0;
    return beacon_payload_lut[mode];
}

sterg_uhf_beacon_t *sterg_uhf_beacon_create(void) {
    sterg_uhf_beacon_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;

    b->rs = correct_reed_solomon_create(
        STERG_RS_PRIMITIVE_POLY,
        STERG_RS_FIRST_ROOT,
        STERG_RS_ROOT_GAP,
        STERG_RS_NUM_ROOTS);
    if (!b->rs) { free(b); return NULL; }

    return b;
}

void sterg_uhf_beacon_destroy(sterg_uhf_beacon_t *b) {
    if (!b) return;
    if (b->rs) correct_reed_solomon_destroy(b->rs);
    free(b);
}

size_t sterg_uhf_beacon_encode(sterg_uhf_beacon_t *b,
                               sterg_beacon_mode_t mode,
                               const uint8_t *payload,
                               uint8_t *encoded) {
    if (!b || !payload || !encoded) return 0;

    size_t payload_len = sterg_beacon_payload_len(mode);
    if (payload_len == 0) return 0;

    /* libcorrect's RS encode accepts any msg_length <= 223 and produces
     * msg_length + 32 bytes of output (shortened code — virtual zero
     * padding up to 223 is not emitted). Matches CCSDS 131.0-B-5 §4. */
    ssize_t rs_len = correct_reed_solomon_encode(
        b->rs, payload, payload_len, encoded);
    if (rs_len < 0) return 0;

    return payload_len + STERG_BEACON_PARITY;
}

ssize_t sterg_uhf_beacon_decode(sterg_uhf_beacon_t *b,
                                sterg_beacon_mode_t mode,
                                const uint8_t *encoded,
                                uint8_t *payload) {
    if (!b || !encoded || !payload) return -1;

    size_t payload_len = sterg_beacon_payload_len(mode);
    if (payload_len == 0) return -1;

    size_t encoded_len = payload_len + STERG_BEACON_PARITY;

    return correct_reed_solomon_decode(
        b->rs, encoded, encoded_len, payload);
}
