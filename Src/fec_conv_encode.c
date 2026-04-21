#include "conv_internal.h"

size_t correct_convolutional_encode_len(correct_convolutional *conv, size_t msg_len) {
    size_t msgbits = 8 * msg_len;
    size_t encodedbits = conv->rate * (msgbits + conv->order + 1);
    return encodedbits;
}

// shift in most significant bit every time, one byte at a time
size_t correct_convolutional_encode(correct_convolutional *conv,
                                    const uint8_t *msg,
                                    size_t msg_len,
                                    uint8_t *encoded) {
    shift_register_t shiftregister = 0;

    unsigned int shiftmask = (1 << conv->order) - 1;

    size_t encoded_len_bits = correct_convolutional_encode_len(conv, msg_len);
    size_t encoded_len = (encoded_len_bits % 8) ? (encoded_len_bits / 8 + 1) : (encoded_len_bits / 8);
    bit_writer_reconfigure(conv->bit_writer, encoded, encoded_len);

    bit_reader_reconfigure(conv->bit_reader, msg, msg_len);

    for (size_t i = 0; i < 8 * msg_len; i++) {
        shiftregister <<= 1;
        shiftregister |= bit_reader_read(conv->bit_reader, 1);
        shiftregister &= shiftmask;

        unsigned int out = conv->table[shiftregister];
        bit_writer_write(conv->bit_writer, out, conv->rate);
    }

    // flush the shiftregister (all 0 inputs)
    for (size_t i = 0; i < conv->order + 1; i++) {
        shiftregister <<= 1;
        shiftregister &= shiftmask;
        unsigned int out = conv->table[shiftregister];
        bit_writer_write(conv->bit_writer, out, conv->rate);
    }

    bit_writer_flush_byte(conv->bit_writer);

    return encoded_len_bits;
}
