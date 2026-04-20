#include "correct/convolutional/lookup.h"

// table has numstates rows
// each row contains all of the polynomial output bits concatenated together
// the first poly gets the LEAST significant bit, last poly gets most significant
void fill_table(unsigned int rate,
                unsigned int order,
                const polynomial_t *poly,
                unsigned int *table) {
    for (shift_register_t i = 0; i < (1U << order); i++) {
        unsigned int out = 0;
        unsigned int mask = 1;
        for (size_t j = 0; j < rate; j++) {
            out |= (popcount(i & poly[j]) % 2) ? mask : 0;
            mask <<= 1;
        }
        table[i] = out;
    }
}

pair_lookup_t pair_lookup_create(unsigned int rate,
                                 unsigned int order,
                                 const unsigned int *table) {
    pair_lookup_t pairs;

    pairs.keys = malloc(sizeof(unsigned int) * ((size_t)1 << (order - 1)));
    pairs.outputs = calloc(((size_t)1 << (rate * 2)), sizeof(unsigned int));
    unsigned int *inv_outputs = calloc(((size_t)1 << (rate * 2)), sizeof(unsigned int));
    unsigned int output_counter = 1;
    for (unsigned int i = 0; i < (1U << (order - 1)); i++) {
        unsigned int out = table[i * 2 + 1];
        out <<= rate;
        out |= table[i * 2];

        if (!inv_outputs[out]) {
            inv_outputs[out] = output_counter;
            pairs.outputs[output_counter] = out;
            output_counter++;
        }
        pairs.keys[i] = inv_outputs[out];
    }
    pairs.outputs_len = output_counter;
    pairs.output_mask = (1U << (rate)) - 1;
    pairs.output_width = rate;
    pairs.distances = calloc(pairs.outputs_len, sizeof(distance_pair_t));
    free(inv_outputs);
    return pairs;
}

void pair_lookup_destroy(pair_lookup_t pairs) {
    free(pairs.keys);
    free(pairs.outputs);
    free(pairs.distances);
}

void pair_lookup_fill_distance(pair_lookup_t pairs, distance_t *distances) {
    for (unsigned int i = 1; i < pairs.outputs_len; i += 1) {
        output_pair_t concat_out = pairs.outputs[i];
        unsigned int i_0 = concat_out & pairs.output_mask;
        concat_out >>= pairs.output_width;
        unsigned int i_1 = concat_out;

        pairs.distances[i] = (distances[i_1] << 16) | distances[i_0];
    }
}
