#ifndef CORRECT_REED_SOLOMON_FIELD
#define CORRECT_REED_SOLOMON_FIELD
#include "correct/reed-solomon.h"

static inline field_element_t field_mul_log_element(field_t field, field_logarithm_t l, field_logarithm_t r) {
    field_operation_t res = (field_operation_t)l + (field_operation_t)r;
    return field.exp[res];
}

static inline field_t field_create(field_operation_t primitive_poly) {
    field_element_t *exp = malloc(512 * sizeof(field_element_t));
    field_logarithm_t *log = malloc(256 * sizeof(field_logarithm_t));

    field_operation_t element = 1;
    exp[0] = (field_element_t)element;
    log[0] = (field_logarithm_t)0;  // undefined, should never be accessed
    for (field_operation_t i = 1; i < 512; i++) {
        element = element * 2;
        element = (element > 255) ? (element ^ primitive_poly) : element;
        exp[i] = (field_element_t)element;
        if (i < 256) {
            log[element] = (field_logarithm_t)i;
        }
    }

    field_t field;
    *(field_element_t **)&field.exp = exp;
    *(field_logarithm_t **)&field.log = log;

    return field;
}

static inline void field_destroy(field_t field) {
    free(*(field_element_t **)&field.exp);
    free(*(field_element_t **)&field.log);
}

static inline field_element_t field_add(field_t field, field_element_t l, field_element_t r) {
    (void)field;
    return l ^ r;
}

static inline field_element_t field_sub(field_t field, field_element_t l, field_element_t r) {
    (void)field;
    return l ^ r;
}

static inline field_element_t field_sum(field_t field, field_element_t elem, unsigned int n) {
    (void)field;
    return (n % 2) ? elem : 0;
}

static inline field_element_t field_mul(field_t field, field_element_t l, field_element_t r) {
    if (l == 0 || r == 0) {
        return 0;
    }
    field_operation_t res = (field_operation_t)field.log[l] + (field_operation_t)field.log[r];
    return field.exp[res];
}

static inline field_element_t field_div(field_t field, field_element_t l, field_element_t r) {
    if (l == 0) {
        return 0;
    }

    if (r == 0) {
        return 0;
    }

    field_operation_t res = (field_operation_t)255 + (field_operation_t)field.log[l] - (field_operation_t)field.log[r];
    return field.exp[res];
}

static inline field_logarithm_t field_mul_log(field_t field, field_logarithm_t l, field_logarithm_t r) {
    (void)field;
    field_operation_t res = (field_operation_t)l + (field_operation_t)r;

    if (res > 255) {
        return (field_logarithm_t)(res - 255);
    }
    return (field_logarithm_t)res;
}

static inline field_logarithm_t field_div_log(field_t field, field_logarithm_t l, field_logarithm_t r) {
    (void)field;
    field_operation_t res = (field_operation_t)255 + (field_operation_t)l - (field_operation_t)r;
    if (res > 255) {
        return (field_logarithm_t)(res - 255);
    }
    return (field_logarithm_t)res;
}

static inline field_element_t field_pow(field_t field, field_element_t elem, int pow) {
    field_logarithm_t log = field.log[elem];
    int res_log = log * pow;
    int mod = res_log % 255;
    if (mod < 0) {
        mod += 255;
    }
    return field.exp[mod];
}
#endif
