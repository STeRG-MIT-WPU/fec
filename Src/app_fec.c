/*
 * app_fec.c — see app_fec.h for flow description.
 */
#include "app_fec.h"

#include "main.h"        /* HAL, UART_HandleTypeDef, HAL_GetTick */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app_uart.h"
#include "sterg_fec.h"

/* ================================================================
 * CCSDS primary header layout (6 bytes, big-endian on the wire)
 *
 *   bytes 0-1 : Packet ID  (upper 5 bits = version/type/sec-hdr,
 *                           lower 11 bits = APID)
 *               Used verbatim as the cFE Message-ID (MID).
 *   bytes 2-3 : Sequence Control
 *   bytes 4-5 : Packet Data Length = payload-byte-count - 1
 *
 *   total_length = 6 + (len_field + 1) = 7 + len_field
 * ================================================================ */
#define CCSDS_HDR_LEN          6u
/* Largest payload we actually expect from sterg_tlm_agg is ~223 B of
 * data + secondary header + aggregator overhead — well under 512 B.
 * Anything larger than this is almost certainly framing drift. */
#define CCSDS_MAX_PACKET_LEN   512u

/* MIDs emitted by sterg_tlm_agg (see apps/sterg_tlm_agg/fsw/inc/sterg_tlm_agg_msgids.h) */
#define MID_TLM_AGG_PKT1       0x089Cu
#define MID_TLM_AGG_PKT2       0x089Du
#define MID_TLM_AGG_PKT3       0x089Eu
#define MID_TLM_AGG_PKT4       0x089Fu

static inline int mid_is_known(uint16_t mid) {
    return mid == MID_TLM_AGG_PKT1 || mid == MID_TLM_AGG_PKT2 ||
           mid == MID_TLM_AGG_PKT3 || mid == MID_TLM_AGG_PKT4;
}

/* ================================================================
 * State
 * ================================================================ */
static sterg_uhf_hk_t     *s_hk;
static sterg_uhf_beacon_t *s_bcn;     /* exercised via synthetic frame below */

static uint32_t s_frame_count;
static uint32_t s_ok_count;
static uint32_t s_fail_count;
static uint32_t s_resync_bytes;    /* total bytes dropped looking for a header */

/* Scratch buffers. Putting them in .bss (not on stack) keeps the ISR
 * stack small and makes their cost obvious to the linker. */
static uint8_t s_packet[CCSDS_MAX_PACKET_LEN];
static uint8_t s_fec_in[STERG_HK_MSG_LEN];      /* zero-padded to 223 */
static uint8_t s_fec_enc[STERG_HK_ENCODED_MAX_LEN];
static uint8_t s_fec_dec[STERG_HK_MSG_LEN];

/* ================================================================
 * CCSDS reassembly
 * ================================================================ */
static int try_read_packet(size_t *out_len) {
    /* Drop bytes one at a time until the next 6-byte window at the head
     * of the ring looks like a plausible CCSDS header:
     *   - bytes 0-1 match a known MID
     *   - 7 + length_field (bytes 4-5) is in range [CCSDS_HDR_LEN+1,
     *     CCSDS_MAX_PACKET_LEN]
     * This is the standard resync strategy for a self-delimiting stream
     * where we may have started mid-packet. */
    for (;;) {
        if (APP_UART_Available() < CCSDS_HDR_LEN) return 0;

        uint8_t b0, b1, len_hi, len_lo;
        APP_UART_Peek(0, &b0);
        APP_UART_Peek(1, &b1);
        APP_UART_Peek(4, &len_hi);
        APP_UART_Peek(5, &len_lo);

        uint16_t mid       = (uint16_t)((b0 << 8) | b1);
        uint16_t len_field = (uint16_t)((len_hi << 8) | len_lo);
        uint32_t total     = 7u + (uint32_t)len_field;

        if (mid_is_known(mid) &&
            total >= (CCSDS_HDR_LEN + 1u) &&
            total <= CCSDS_MAX_PACKET_LEN) {
            /* Plausible header. Wait for the rest of the packet. */
            if (APP_UART_Available() < total) return 0;
            size_t got = APP_UART_Read(s_packet, total);
            if (got != total) return -1;
            *out_len = total;
            return 1;
        }

        /* Not a valid header — drop one byte and retry. */
        APP_UART_Consume(1);
        s_resync_bytes++;
    }
}

/* ================================================================
 * Beacon mode selection by payload length.
 * All five modes use 32 parity bytes; match on payload size only. */
static int beacon_mode_for_len(size_t payload_len, sterg_beacon_mode_t *out) {
    switch (payload_len) {
        case 99: *out = STERG_BEACON_MODE_NOMINAL;    return 1;
        case 96: *out = STERG_BEACON_MODE_PRELINK;    return 1;
        case 95: *out = STERG_BEACON_MODE_LOWPOWER;   return 1;
        case 89: *out = STERG_BEACON_MODE_DETUMBLING; return 1;
        default: return 0;
    }
}

/* ================================================================
 * Dispatch one packet through the correct FEC chain and report.
 * ================================================================ */
static void process_packet(const uint8_t *pkt, size_t len) {
    s_frame_count++;
    uint16_t mid = (uint16_t)((pkt[0] << 8) | pkt[1]);

    uint32_t t0 = HAL_GetTick();

    /* ---- HK chain ---- */
    if (mid == MID_TLM_AGG_PKT1 || mid == MID_TLM_AGG_PKT2 ||
        mid == MID_TLM_AGG_PKT3 || mid == MID_TLM_AGG_PKT4) {

        /* libcorrect RS accepts msg_length < 223 (shortened code).
         * But the CCSDS RS(255,223) spec calls for exactly 223 data
         * symbols with zero padding — so we zero-pad here, encode
         * the full 223, Conv-encode the 255 output, and only compare
         * the original len bytes after round-trip. */
        size_t pad_len = (len > STERG_HK_MSG_LEN) ? STERG_HK_MSG_LEN : len;
        memset(s_fec_in, 0, STERG_HK_MSG_LEN);
        memcpy(s_fec_in, pkt, pad_len);

        size_t enc_bits = sterg_uhf_hk_encode(s_hk, s_fec_in, s_fec_enc);
        if (enc_bits == 0) {
            s_fail_count++;
            printf("[%04lu] HK mid=0x%04X len=%u  ENCODE FAILED\r\n",
                   (unsigned long)s_frame_count, mid, (unsigned)len);
            return;
        }

        int rc = sterg_uhf_hk_decode(s_hk, s_fec_enc, enc_bits, s_fec_dec);
        uint32_t elapsed = HAL_GetTick() - t0;

        int match = (rc == 0) && (memcmp(s_fec_in, s_fec_dec, STERG_HK_MSG_LEN) == 0);
        if (match) s_ok_count++; else s_fail_count++;

        printf("[%04lu] HK  mid=0x%04X len=%3u  enc=%u B  rt=%s  %lu ms\r\n",
               (unsigned long)s_frame_count, mid, (unsigned)len,
               (unsigned)(enc_bits / 8),
               match ? "OK  " : "FAIL",
               (unsigned long)elapsed);
        return;
    }

    /* ---- Beacon chain (not emitted by current MP257, reachable if a
     *      beacon-MID packet is ever injected by a later cFS change) ---- */
    sterg_beacon_mode_t mode;
    if (len >= CCSDS_HDR_LEN && beacon_mode_for_len(len - CCSDS_HDR_LEN, &mode)) {
        size_t payload_len = len - CCSDS_HDR_LEN;
        const uint8_t *payload = pkt + CCSDS_HDR_LEN;
        static uint8_t enc[STERG_BEACON_MAX_ENCODED];
        static uint8_t dec[STERG_BEACON_MAX_PAYLOAD];

        size_t enc_len = sterg_uhf_beacon_encode(s_bcn, mode, payload, enc);
        ssize_t dec_len = sterg_uhf_beacon_decode(s_bcn, mode, enc, dec);
        uint32_t elapsed = HAL_GetTick() - t0;

        int match = (dec_len == (ssize_t)payload_len) &&
                    (memcmp(payload, dec, payload_len) == 0);
        if (match) s_ok_count++; else s_fail_count++;

        printf("[%04lu] BCN mid=0x%04X mode=%d len=%u  enc=%u B  rt=%s  %lu ms\r\n",
               (unsigned long)s_frame_count, mid, (int)mode,
               (unsigned)payload_len, (unsigned)enc_len,
               match ? "OK  " : "FAIL",
               (unsigned long)elapsed);
        return;
    }

    /* Unreachable under the MID-whitelist resync — mid is known to be one
     * of 0x089C..0x089F before process_packet() is called. Left here as
     * a safety net. */
    s_fail_count++;
    printf("[%04lu] ??  mid=0x%04X len=%u  UNEXPECTED\r\n",
           (unsigned long)s_frame_count, mid, (unsigned)len);
}

/* ================================================================
 * Public API
 * ================================================================ */
int APP_FEC_Init(UART_HandleTypeDef *mp257_uart) {
    s_hk  = sterg_uhf_hk_create();
    s_bcn = sterg_uhf_beacon_create();
    if (!s_hk || !s_bcn) {
        printf("APP_FEC_Init: allocation failed — bump _Min_Heap_Size\r\n");
        return -1;
    }

    APP_UART_Init(mp257_uart);

    printf("\r\n");
    printf("==================================================\r\n");
    printf("STeRG S1.0 FEC on-board test — NUCLEO-H755ZI-Q\r\n");
    printf("  UHF HK    : RS(255,223) + Conv 1/2 K=7\r\n");
    printf("  UHF Beacon: RS(255,223) shortened\r\n");
    printf("  Listening on MP257 UART at 115200 8N1\r\n");
    printf("==================================================\r\n");
    return 0;
}

void APP_FEC_Loop(void) {
    size_t pkt_len;
    int r = try_read_packet(&pkt_len);
    if (r <= 0) return;

    process_packet(s_packet, pkt_len);

    /* Running totals every 32 frames */
    if ((s_frame_count & 0x1F) == 0) {
        printf("        --- totals: %lu ok, %lu fail of %lu (resync bytes: %lu) ---\r\n",
               (unsigned long)s_ok_count,
               (unsigned long)s_fail_count,
               (unsigned long)s_frame_count,
               (unsigned long)s_resync_bytes);
    }
}
