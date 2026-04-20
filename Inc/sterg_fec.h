/*
 * sterg_fec.h — STeRG S1.0 UHF FEC chains for STM32
 *
 * Implements the two coding configurations defined in FEC-FINAL-002:
 *
 *   UHF HK  (VCID 0): RS(255,223) + Conv 1/2 K=7   — CCSDS 131.0-B-5   r=0.437
 *   UHF BCN (VCID 7): RS(255,223) shortened        — CCSDS 131.0-B-5 §4 r≈0.75
 *
 * S-band LDPC 1/2 is not implemented here — it lives on the Zynq-7000
 * FPGA, not on the STM32.
 *
 * Internally wraps the underlying libcorrect-derived codecs with
 * CCSDS-compliant parameters (primitive poly 0x187, first root 112,
 * root gap 11 for RS; polynomial {0155,0117} for Conv).
 */
#ifndef STERG_FEC_H
#define STERG_FEC_H

#include <stdint.h>
#include <stddef.h>

#include "correct.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * UHF HK — RS(255,223) + Conv 1/2 K=7
 * ===================================================================== */

#define STERG_HK_MSG_LEN          223   /* RS data symbols */
#define STERG_HK_RS_BLOCK_LEN     255   /* RS code block   */
#define STERG_HK_RS_PARITY        32    /* 2t             */
/* Conv output: rate=2 * (223*8 + 7+1 tail) = 2*1792 = 3584 bits; ceil/8 = 448 bytes.
 * Actually 223 byte RS output is 255 bytes then Conv at rate 1/2 K=7:
 *   bits_in  = 255 * 8 = 2040
 *   bits_out = 2 * (2040 + 7 + 1) = 4096     ->  512 bytes */
#define STERG_HK_ENCODED_MAX_LEN  512

typedef struct sterg_uhf_hk sterg_uhf_hk_t;

/* Allocates and initialises an HK encoder+decoder. Returns NULL on OOM. */
sterg_uhf_hk_t *sterg_uhf_hk_create(void);
void sterg_uhf_hk_destroy(sterg_uhf_hk_t *h);

/* Encode one 223-byte HK frame through RS then Conv.
 *   msg      : 223 input bytes
 *   encoded  : buffer of at least STERG_HK_ENCODED_MAX_LEN bytes
 * Returns the number of bits written (always 4096 for a full HK frame), 0 on error. */
size_t sterg_uhf_hk_encode(sterg_uhf_hk_t *h,
                           const uint8_t msg[STERG_HK_MSG_LEN],
                           uint8_t *encoded);

/* Decode one HK frame.
 *   encoded    : byte buffer produced by sterg_uhf_hk_encode
 *   num_bits   : value returned by sterg_uhf_hk_encode
 *   msg        : 223-byte output buffer
 * Returns  0  on success, -1 on Reed-Solomon failure. */
int sterg_uhf_hk_decode(sterg_uhf_hk_t *h,
                        const uint8_t *encoded, size_t num_bits,
                        uint8_t msg[STERG_HK_MSG_LEN]);


/* =====================================================================
 * UHF Beacon — RS(255,223) shortened, no Conv
 * ===================================================================== */

/* Beacon modes — payload sizes from the FEC_FINAL_DECISION spec (§5.5).
 * All modes produce payload + 32 parity bytes on the wire. */
typedef enum {
    STERG_BEACON_MODE_NOMINAL    = 0,  /* 99 B payload → RS(131,99) */
    STERG_BEACON_MODE_PRELINK    = 1,  /* 96 B payload → RS(128,96) */
    STERG_BEACON_MODE_DETUMBLING = 2,  /* 89 B payload → RS(121,89) */
    STERG_BEACON_MODE_LOWPOWER   = 3,  /* 95 B payload → RS(127,95) */
    STERG_BEACON_MODE_CRITICAL   = 4,  /* 89 B payload → RS(121,89) */
} sterg_beacon_mode_t;

#define STERG_BEACON_PARITY       32   /* every mode uses 32 parity bytes */
#define STERG_BEACON_MAX_PAYLOAD  99   /* longest mode (0) */
#define STERG_BEACON_MAX_ENCODED  (STERG_BEACON_MAX_PAYLOAD + STERG_BEACON_PARITY)

typedef struct sterg_uhf_beacon sterg_uhf_beacon_t;

/* Allocates and initialises a beacon encoder+decoder. */
sterg_uhf_beacon_t *sterg_uhf_beacon_create(void);
void sterg_uhf_beacon_destroy(sterg_uhf_beacon_t *b);

/* Return the payload length for a given mode, or 0 if mode is invalid. */
size_t sterg_beacon_payload_len(sterg_beacon_mode_t mode);

/* Encode a beacon.
 *   mode        : one of STERG_BEACON_MODE_*
 *   payload     : mode-specific number of bytes (see sterg_beacon_payload_len)
 *   encoded     : buffer of at least payload_len + STERG_BEACON_PARITY bytes
 * Returns total bytes written (payload_len + 32), 0 on invalid mode. */
size_t sterg_uhf_beacon_encode(sterg_uhf_beacon_t *b,
                               sterg_beacon_mode_t mode,
                               const uint8_t *payload,
                               uint8_t *encoded);

/* Decode a beacon.
 *   mode         : same mode that was used to encode
 *   encoded      : encoded buffer of (payload_len + 32) bytes
 *   payload      : output buffer for payload_len bytes
 * Returns payload_len on success, -1 on Reed-Solomon failure. */
ssize_t sterg_uhf_beacon_decode(sterg_uhf_beacon_t *b,
                                sterg_beacon_mode_t mode,
                                const uint8_t *encoded,
                                uint8_t *payload);

#ifdef __cplusplus
}
#endif

#endif /* STERG_FEC_H */
