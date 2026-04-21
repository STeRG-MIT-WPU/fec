#include "rs_types.h"
#include "rs_field.h"
#include "rs_polynomial.h"

// coeff must be of size nroots + 1
static polynomial_t reed_solomon_build_generator(field_t field, unsigned int nroots,
                                                 field_element_t first_consecutive_root,
                                                 unsigned int root_gap, polynomial_t generator,
                                                 field_element_t *roots) {
    (void)generator;
    for (unsigned int i = 0; i < nroots; i++) {
        roots[i] = field.exp[(root_gap * (i + first_consecutive_root)) % 255];
    }
    return polynomial_create_from_roots(field, nroots, roots);
}

correct_reed_solomon *correct_reed_solomon_create(field_operation_t primitive_polynomial,
                                                  field_logarithm_t first_consecutive_root,
                                                  field_logarithm_t generator_root_gap,
                                                  size_t num_roots) {
    correct_reed_solomon *rs = calloc(1, sizeof(correct_reed_solomon));
    if (!rs) return NULL;
    rs->field = field_create(primitive_polynomial);

    rs->block_length = 255;
    rs->min_distance = num_roots;
    rs->message_length = rs->block_length - rs->min_distance;

    rs->first_consecutive_root = first_consecutive_root;
    rs->generator_root_gap = generator_root_gap;

    rs->generator_roots = malloc(rs->min_distance * sizeof(field_element_t));

    rs->generator = reed_solomon_build_generator(rs->field, rs->min_distance,
                                                 rs->first_consecutive_root, rs->generator_root_gap,
                                                 rs->generator, rs->generator_roots);

    rs->encoded_polynomial = polynomial_create(rs->block_length - 1);
    rs->encoded_remainder = polynomial_create(rs->block_length - 1);

    rs->has_init_decode = false;

    return rs;
}

void correct_reed_solomon_destroy(correct_reed_solomon *rs) {
    field_destroy(rs->field);
    polynomial_destroy(rs->generator);
    free(rs->generator_roots);
    polynomial_destroy(rs->encoded_polynomial);
    polynomial_destroy(rs->encoded_remainder);
    if (rs->has_init_decode) {
        free(rs->syndromes);
        free(rs->modified_syndromes);
        polynomial_destroy(rs->received_polynomial);
        polynomial_destroy(rs->error_locator);
        polynomial_destroy(rs->error_locator_log);
        polynomial_destroy(rs->erasure_locator);
        free(rs->error_roots);
        free(rs->error_vals);
        free(rs->error_locations);
        polynomial_destroy(rs->last_error_locator);
        polynomial_destroy(rs->error_evaluator);
        polynomial_destroy(rs->error_locator_derivative);
        for (unsigned int i = 0; i < rs->min_distance; i++) {
            free(rs->generator_root_exp[i]);
        }
        free(rs->generator_root_exp);
        for (field_operation_t i = 0; i < 256; i++) {
            free(rs->element_exp[i]);
        }
        free(rs->element_exp);
        polynomial_destroy(rs->init_from_roots_scratch[0]);
        polynomial_destroy(rs->init_from_roots_scratch[1]);
    }
    free(rs);
}
