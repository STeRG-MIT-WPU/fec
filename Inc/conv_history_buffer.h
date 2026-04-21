#ifndef CORRECT_CONVOLUTIONAL_HISTORY_BUFFER_H
#define CORRECT_CONVOLUTIONAL_HISTORY_BUFFER_H
#include "conv_types.h"
#include "conv_bit.h"

// ring buffer of path histories
// generates output bits after accumulating sufficient history
typedef struct {
    // history entries must be at least this old to be decoded
    const unsigned int min_traceback_length;
    // we'll decode entries in bursts. this tells us the length of the burst
    const unsigned int traceback_group_length;
    // we will store a total of cap entries
    const unsigned int cap;

    const unsigned int num_states;
    const shift_register_t highbit;

    uint8_t **history;

    unsigned int index;
    unsigned int len;

    uint8_t *fetched;

    unsigned int renormalize_interval;
    unsigned int renormalize_counter;
} history_buffer;

history_buffer *history_buffer_create(unsigned int min_traceback_length,
                                      unsigned int traceback_group_length,
                                      unsigned int renormalize_interval,
                                      unsigned int num_states,
                                      shift_register_t highbit);
void history_buffer_destroy(history_buffer *buf);
void history_buffer_reset(history_buffer *buf);
void history_buffer_step(history_buffer *buf);
uint8_t *history_buffer_get_slice(history_buffer *buf);
shift_register_t history_buffer_search(history_buffer *buf,
                                       const distance_t *distances,
                                       unsigned int search_every);
void history_buffer_traceback(history_buffer *buf, shift_register_t bestpath,
                              unsigned int min_traceback_length,
                              bit_writer_t *output);
void history_buffer_process_skip(history_buffer *buf, distance_t *distances,
                                 bit_writer_t *output, unsigned int skip);
void history_buffer_process(history_buffer *buf, distance_t *distances,
                            bit_writer_t *output);
void history_buffer_flush(history_buffer *buf, bit_writer_t *output);
#endif
