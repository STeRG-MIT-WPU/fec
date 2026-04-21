#include "rs_types.h"
#include "rs_field.h"
#include "rs_polynomial.h"

// calculate all syndromes of the received polynomial at the roots of the generator
// returns true if syndromes are all zero
static bool reed_solomon_find_syndromes(field_t field, polynomial_t msgpoly, field_logarithm_t **generator_root_exp,
                                        field_element_t *syndromes, size_t min_distance) {
    bool all_zero = true;
    memset(syndromes, 0, min_distance * sizeof(field_element_t));
    for (unsigned int i = 0; i < min_distance; i++) {
        field_element_t eval = polynomial_eval_lut(field, msgpoly, generator_root_exp[i]);
        if (eval) {
            all_zero = false;
        }
        syndromes[i] = eval;
    }
    return all_zero;
}

// Berlekamp-Massey algorithm to find LFSR that describes syndromes
static unsigned int reed_solomon_find_error_locator(correct_reed_solomon *rs, size_t num_erasures) {
    unsigned int numerrors = 0;

    memset(rs->error_locator.coeff, 0, (rs->min_distance + 1) * sizeof(field_element_t));

    rs->error_locator.coeff[0] = 1;
    rs->error_locator.order = 0;

    memcpy(rs->last_error_locator.coeff, rs->error_locator.coeff, (rs->min_distance + 1) * sizeof(field_element_t));
    rs->last_error_locator.order = rs->error_locator.order;

    field_element_t discrepancy;
    field_element_t last_discrepancy = 1;
    unsigned int delay_length = 1;

    for (unsigned int i = rs->error_locator.order; i < rs->min_distance - num_erasures; i++) {
        discrepancy = rs->syndromes[i];
        for (unsigned int j = 1; j <= numerrors; j++) {
            discrepancy = field_add(rs->field, discrepancy,
                                    field_mul(rs->field, rs->error_locator.coeff[j], rs->syndromes[i - j]));
        }

        if (!discrepancy) {
            delay_length++;
            continue;
        }

        if (2 * numerrors <= i) {
            for (int j = rs->last_error_locator.order; j >= 0; j--) {
                rs->last_error_locator.coeff[j + delay_length] = field_div(
                    rs->field, field_mul(rs->field, rs->last_error_locator.coeff[j], discrepancy), last_discrepancy);
            }
            for (int j = delay_length - 1; j >= 0; j--) {
                rs->last_error_locator.coeff[j] = 0;
            }

            field_element_t temp;
            for (int j = 0; j <= (int)(rs->last_error_locator.order + delay_length); j++) {
                temp = rs->error_locator.coeff[j];
                rs->error_locator.coeff[j] =
                    field_add(rs->field, rs->error_locator.coeff[j], rs->last_error_locator.coeff[j]);
                rs->last_error_locator.coeff[j] = temp;
            }
            unsigned int temp_order = rs->error_locator.order;
            rs->error_locator.order = rs->last_error_locator.order + delay_length;
            rs->last_error_locator.order = temp_order;

            numerrors = i + 1 - numerrors;
            last_discrepancy = discrepancy;
            delay_length = 1;
            continue;
        }

        for (int j = rs->last_error_locator.order; j >= 0; j--) {
            rs->error_locator.coeff[j + delay_length] =
                field_add(rs->field, rs->error_locator.coeff[j + delay_length],
                          field_div(rs->field, field_mul(rs->field, rs->last_error_locator.coeff[j], discrepancy),
                                    last_discrepancy));
        }
        rs->error_locator.order = (rs->last_error_locator.order + delay_length > rs->error_locator.order)
                                      ? rs->last_error_locator.order + delay_length
                                      : rs->error_locator.order;
        delay_length++;
    }
    return rs->error_locator.order;
}

// find the roots of the error locator polynomial - Chien search
bool reed_solomon_factorize_error_locator(field_t field, unsigned int num_skip, polynomial_t locator_log, field_element_t *roots,
                                          field_logarithm_t **element_exp) {
    unsigned int root = num_skip;
    memset(roots + num_skip, 0, (locator_log.order) * sizeof(field_element_t));
    for (field_operation_t i = 0; i < 256; i++) {
        if (!polynomial_eval_log_lut(field, locator_log, element_exp[i])) {
            roots[root] = (field_element_t)i;
            root++;
        }
    }
    return root == locator_log.order + num_skip;
}

// use error locator and syndromes to find the error evaluator polynomial
void reed_solomon_find_error_evaluator(field_t field, polynomial_t locator, polynomial_t syndromes,
                                       polynomial_t error_evaluator) {
    polynomial_mul(field, locator, syndromes, error_evaluator);
}

// Forney algorithm
void reed_solomon_find_error_values(correct_reed_solomon *rs) {
    polynomial_t syndrome_poly;
    syndrome_poly.order = rs->min_distance - 1;
    syndrome_poly.coeff = rs->syndromes;
    memset(rs->error_evaluator.coeff, 0, (rs->error_evaluator.order + 1) * sizeof(field_element_t));
    reed_solomon_find_error_evaluator(rs->field, rs->error_locator, syndrome_poly, rs->error_evaluator);

    rs->error_locator_derivative.order = rs->error_locator.order - 1;
    polynomial_formal_derivative(rs->field, rs->error_locator, rs->error_locator_derivative);

    for (unsigned int i = 0; i < rs->error_locator.order; i++) {
        if (rs->error_roots[i] == 0) {
            continue;
        }
        rs->error_vals[i] = field_mul(
            rs->field, field_pow(rs->field, rs->error_roots[i], rs->first_consecutive_root - 1),
            field_div(
                rs->field, polynomial_eval_lut(rs->field, rs->error_evaluator, rs->element_exp[rs->error_roots[i]]),
                polynomial_eval_lut(rs->field, rs->error_locator_derivative, rs->element_exp[rs->error_roots[i]])));
    }
}

void reed_solomon_find_error_locations(field_t field, field_logarithm_t generator_root_gap,
                                       field_element_t *error_roots, field_logarithm_t *error_locations,
                                       unsigned int num_errors, unsigned int num_skip) {
    for (unsigned int i = 0; i < num_errors; i++) {
        if (error_roots[i] == 0) {
            continue;
        }

        field_operation_t loc = field_div(field, 1, error_roots[i]);
        for (field_operation_t j = 0; j < 256; j++) {
            if (field_pow(field, j, generator_root_gap) == loc) {
                error_locations[i] = field.log[j];
                break;
            }
        }
    }
    (void)num_skip;
}

// erasure method -- take given locations and convert to roots
static void reed_solomon_find_error_roots_from_locations(field_t field, field_logarithm_t generator_root_gap,
                                                         const field_logarithm_t *error_locations,
                                                         field_element_t *error_roots, unsigned int num_errors) {
    for (unsigned int i = 0; i < num_errors; i++) {
        field_element_t loc = field_pow(field, field.exp[error_locations[i]], generator_root_gap);
        error_roots[i] = field_div(field, 1, loc);
    }
}

// erasure method -- given the roots of the error locator, create the polynomial
static polynomial_t reed_solomon_find_error_locator_from_roots(field_t field, unsigned int num_errors,
                                                               field_element_t *error_roots,
                                                               polynomial_t error_locator,
                                                               polynomial_t *scratch) {
    return polynomial_init_from_roots(field, num_errors, error_roots, error_locator, scratch);
}

static void reed_solomon_find_modified_syndromes(correct_reed_solomon *rs, field_element_t *syndromes, polynomial_t error_locator, field_element_t *modified_syndromes) {
    polynomial_t syndrome_poly;
    syndrome_poly.order = rs->min_distance - 1;
    syndrome_poly.coeff = syndromes;

    polynomial_t modified_syndrome_poly;
    modified_syndrome_poly.order = rs->min_distance - 1;
    modified_syndrome_poly.coeff = modified_syndromes;

    polynomial_mul(rs->field, error_locator, syndrome_poly, modified_syndrome_poly);
}

void correct_reed_solomon_decoder_create(correct_reed_solomon *rs) {
    rs->has_init_decode = true;
    rs->syndromes = calloc(rs->min_distance, sizeof(field_element_t));
    rs->modified_syndromes = calloc(2 * rs->min_distance, sizeof(field_element_t));
    rs->received_polynomial = polynomial_create(rs->block_length - 1);
    rs->error_locator = polynomial_create(rs->min_distance);
    rs->error_locator_log = polynomial_create(rs->min_distance);
    rs->erasure_locator = polynomial_create(rs->min_distance);
    rs->error_roots = calloc(2 * rs->min_distance, sizeof(field_element_t));
    rs->error_vals = malloc(rs->min_distance * sizeof(field_element_t));
    rs->error_locations = malloc(rs->min_distance * sizeof(field_logarithm_t));

    rs->last_error_locator = polynomial_create(rs->min_distance);
    rs->error_evaluator = polynomial_create(rs->min_distance - 1);
    rs->error_locator_derivative = polynomial_create(rs->min_distance - 1);

    // precompute: first block_length powers of every generator root
    // total memory usage is min_distance * block_length bytes e.g. 32 * 255 ~= 8k
    rs->generator_root_exp = malloc(rs->min_distance * sizeof(field_logarithm_t *));
    for (unsigned int i = 0; i < rs->min_distance; i++) {
        rs->generator_root_exp[i] = malloc(rs->block_length * sizeof(field_logarithm_t));
        polynomial_build_exp_lut(rs->field, rs->generator_roots[i], rs->block_length - 1, rs->generator_root_exp[i]);
    }

    // precompute: first min_distance powers of every element in the field
    rs->element_exp = malloc(256 * sizeof(field_logarithm_t *));
    for (field_operation_t i = 0; i < 256; i++) {
        rs->element_exp[i] = malloc(rs->min_distance * sizeof(field_logarithm_t));
        polynomial_build_exp_lut(rs->field, i, rs->min_distance - 1, rs->element_exp[i]);
    }

    rs->init_from_roots_scratch[0] = polynomial_create(rs->min_distance);
    rs->init_from_roots_scratch[1] = polynomial_create(rs->min_distance);
}

ssize_t correct_reed_solomon_decode(correct_reed_solomon *rs, const uint8_t *encoded, size_t encoded_length,
                                    uint8_t *msg) {
    if (encoded_length > rs->block_length) {
        return -1;
    }

    size_t msg_length = encoded_length - rs->min_distance;
    size_t pad_length = rs->block_length - encoded_length;

    if (!rs->has_init_decode) {
        correct_reed_solomon_decoder_create(rs);
    }

    for (unsigned int i = 0; i < encoded_length; i++) {
        rs->received_polynomial.coeff[i] = encoded[encoded_length - (i + 1)];
    }

    for (unsigned int i = 0; i < pad_length; i++) {
        rs->received_polynomial.coeff[i + encoded_length] = 0;
    }


    bool all_zero = reed_solomon_find_syndromes(rs->field, rs->received_polynomial, rs->generator_root_exp,
                                                rs->syndromes, rs->min_distance);

    if (all_zero) {
        for (unsigned int i = 0; i < msg_length; i++) {
            msg[i] = rs->received_polynomial.coeff[encoded_length - (i + 1)];
        }
        return msg_length;
    }

    unsigned int order = reed_solomon_find_error_locator(rs, 0);
    rs->error_locator.order = order;

    for (unsigned int i = 0; i <= rs->error_locator.order; i++) {
        rs->error_locator_log.coeff[i] = rs->field.log[rs->error_locator.coeff[i]];
    }
    rs->error_locator_log.order = rs->error_locator.order;

    if (!reed_solomon_factorize_error_locator(rs->field, 0, rs->error_locator_log, rs->error_roots, rs->element_exp)) {
        return -1;
    }

    reed_solomon_find_error_locations(rs->field, rs->generator_root_gap, rs->error_roots, rs->error_locations,
                                      rs->error_locator.order, 0);

    reed_solomon_find_error_values(rs);

    for (unsigned int i = 0; i < rs->error_locator.order; i++) {
        rs->received_polynomial.coeff[rs->error_locations[i]] =
            field_sub(rs->field, rs->received_polynomial.coeff[rs->error_locations[i]], rs->error_vals[i]);
    }

    for (unsigned int i = 0; i < msg_length; i++) {
        msg[i] = rs->received_polynomial.coeff[encoded_length - (i + 1)];
    }

    return msg_length;
}

ssize_t correct_reed_solomon_decode_with_erasures(correct_reed_solomon *rs, const uint8_t *encoded,
                                                  size_t encoded_length, const uint8_t *erasure_locations,
                                                  size_t erasure_length, uint8_t *msg) {
    if (!erasure_length) {
        return correct_reed_solomon_decode(rs, encoded, encoded_length, msg);
    }

    if (encoded_length > rs->block_length) {
        return -1;
    }

    if (erasure_length > rs->min_distance) {
        return -1;
    }

    size_t msg_length = encoded_length - rs->min_distance;
    size_t pad_length = rs->block_length - encoded_length;

    if (!rs->has_init_decode) {
        correct_reed_solomon_decoder_create(rs);
    }

    for (unsigned int i = 0; i < encoded_length; i++) {
        rs->received_polynomial.coeff[i] = encoded[encoded_length - (i + 1)];
    }

    for (unsigned int i = 0; i < pad_length; i++) {
        rs->received_polynomial.coeff[i + encoded_length] = 0;
    }

    for (unsigned int i = 0; i < erasure_length; i++) {
        rs->error_locations[i] = rs->block_length - (erasure_locations[i] + pad_length + 1);
    }

    reed_solomon_find_error_roots_from_locations(rs->field, rs->generator_root_gap, rs->error_locations,
                                                 rs->error_roots, erasure_length);

    rs->erasure_locator =
        reed_solomon_find_error_locator_from_roots(rs->field, erasure_length, rs->error_roots, rs->erasure_locator, rs->init_from_roots_scratch);

    bool all_zero = reed_solomon_find_syndromes(rs->field, rs->received_polynomial, rs->generator_root_exp,
                                                rs->syndromes, rs->min_distance);

    if (all_zero) {
        for (unsigned int i = 0; i < msg_length; i++) {
            msg[i] = rs->received_polynomial.coeff[encoded_length - (i + 1)];
        }
        return msg_length;
    }

    reed_solomon_find_modified_syndromes(rs, rs->syndromes, rs->erasure_locator, rs->modified_syndromes);

    field_element_t *syndrome_copy = malloc(rs->min_distance * sizeof(field_element_t));
    memcpy(syndrome_copy, rs->syndromes, rs->min_distance * sizeof(field_element_t));

    for (unsigned int i = erasure_length; i < rs->min_distance; i++) {
        rs->syndromes[i - erasure_length] = rs->modified_syndromes[i];
    }

    unsigned int order = reed_solomon_find_error_locator(rs, erasure_length);
    rs->error_locator.order = order;

    for (unsigned int i = 0; i <= rs->error_locator.order; i++) {
        rs->error_locator_log.coeff[i] = rs->field.log[rs->error_locator.coeff[i]];
    }
    rs->error_locator_log.order = rs->error_locator.order;

    if (!reed_solomon_factorize_error_locator(rs->field, erasure_length, rs->error_locator_log, rs->error_roots, rs->element_exp)) {
        free(syndrome_copy);
        return -1;
    }

    polynomial_t temp_poly = polynomial_create(rs->error_locator.order + erasure_length);
    polynomial_mul(rs->field, rs->erasure_locator, rs->error_locator, temp_poly);
    polynomial_t placeholder_poly = rs->error_locator;
    rs->error_locator = temp_poly;

    reed_solomon_find_error_locations(rs->field, rs->generator_root_gap, rs->error_roots, rs->error_locations,
                                      rs->error_locator.order, erasure_length);

    memcpy(rs->syndromes, syndrome_copy, rs->min_distance * sizeof(field_element_t));

    reed_solomon_find_error_values(rs);

    for (unsigned int i = 0; i < rs->error_locator.order; i++) {
        rs->received_polynomial.coeff[rs->error_locations[i]] =
            field_sub(rs->field, rs->received_polynomial.coeff[rs->error_locations[i]], rs->error_vals[i]);
    }

    rs->error_locator = placeholder_poly;

    for (unsigned int i = 0; i < msg_length; i++) {
        msg[i] = rs->received_polynomial.coeff[encoded_length - (i + 1)];
    }

    polynomial_destroy(temp_poly);
    free(syndrome_copy);

    return msg_length;
}
