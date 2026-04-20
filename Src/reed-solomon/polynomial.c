#include "correct/reed-solomon/polynomial.h"

polynomial_t polynomial_create(unsigned int order) {
    polynomial_t polynomial;
    polynomial.coeff = malloc(sizeof(field_element_t) * (order + 1));
    polynomial.order = order;
    return polynomial;
}

void polynomial_destroy(polynomial_t polynomial) {
    free(polynomial.coeff);
}

// if you want a full multiplication, then make res.order = l.order + r.order
// but if you just care about a lower order, e.g. mul mod x^i, then you can select
//    fewer coefficients
void polynomial_mul(field_t field, polynomial_t l, polynomial_t r, polynomial_t res) {
    memset(res.coeff, 0, sizeof(field_element_t) * (res.order + 1));
    for (unsigned int i = 0; i <= l.order; i++) {
        if (i > res.order) {
            continue;
        }
        unsigned int j_limit = (r.order > res.order - i) ? res.order - i : r.order;
        for (unsigned int j = 0; j <= j_limit; j++) {
            res.coeff[i + j] = field_add(field, res.coeff[i + j], field_mul(field, l.coeff[i], r.coeff[j]));
        }
    }
}

void polynomial_mod(field_t field, polynomial_t dividend, polynomial_t divisor, polynomial_t mod) {
    if (mod.order < dividend.order) {
        return;
    }
    memcpy(mod.coeff, dividend.coeff, sizeof(field_element_t) * (dividend.order + 1));

    field_logarithm_t divisor_leading = field.log[divisor.coeff[divisor.order]];
    for (unsigned int i = dividend.order; i > 0; i--) {
        if (i < divisor.order) {
            break;
        }
        if (mod.coeff[i] == 0) {
            continue;
        }
        unsigned int q_order = i - divisor.order;
        field_logarithm_t q_coeff = field_div_log(field, field.log[mod.coeff[i]], divisor_leading);

        for (unsigned int j = 0; j <= divisor.order; j++) {
            if (divisor.coeff[j] == 0) {
                continue;
            }
            mod.coeff[j + q_order] = field_add(field, mod.coeff[j + q_order],
                        field_mul_log_element(field, field.log[divisor.coeff[j]], q_coeff));
        }
    }
}

void polynomial_formal_derivative(field_t field, polynomial_t poly, polynomial_t der) {
    memset(der.coeff, 0, sizeof(field_element_t) * (der.order + 1));
    for (unsigned int i = 0; i <= der.order; i++) {
        der.coeff[i] = field_sum(field, poly.coeff[i + 1], i + 1);
    }
}

field_element_t polynomial_eval(field_t field, polynomial_t poly, field_element_t val) {
    if (val == 0) {
        return poly.coeff[0];
    }

    field_element_t res = 0;

    field_logarithm_t val_exponentiated = field.log[1];
    field_logarithm_t val_log = field.log[val];

    for (unsigned int i = 0; i <= poly.order; i++) {
        if (poly.coeff[i] != 0) {
            res = field_add(field, res,
                    field_mul_log_element(field, field.log[poly.coeff[i]], val_exponentiated));
        }
        val_exponentiated = field_mul_log(field, val_exponentiated, val_log);
    }
    return res;
}

field_element_t polynomial_eval_lut(field_t field, polynomial_t poly, const field_logarithm_t *val_exp) {
    if (val_exp[0] == 0) {
        return poly.coeff[0];
    }

    field_element_t res = 0;

    for (unsigned int i = 0; i <= poly.order; i++) {
        if (poly.coeff[i] != 0) {
            res = field_add(field, res,
                    field_mul_log_element(field, field.log[poly.coeff[i]], val_exp[i]));
        }
    }
    return res;
}

field_element_t polynomial_eval_log_lut(field_t field, polynomial_t poly_log, const field_logarithm_t *val_exp) {
    if (val_exp[0] == 0) {
        if (poly_log.coeff[0] == 0) {
            return 0;
        }
        return field.exp[poly_log.coeff[0]];
    }

    field_element_t res = 0;

    for (unsigned int i = 0; i <= poly_log.order; i++) {
        if (poly_log.coeff[i] != 0) {
            res = field_add(field, res,
                    field_mul_log_element(field, poly_log.coeff[i], val_exp[i]));
        }
    }
    return res;
}

void polynomial_build_exp_lut(field_t field, field_element_t val, unsigned int order, field_logarithm_t *val_exp) {
    field_logarithm_t val_exponentiated = field.log[1];
    field_logarithm_t val_log = field.log[val];
    for (unsigned int i = 0; i <= order; i++) {
        if (val == 0) {
            val_exp[i] = 0;
        } else {
            val_exp[i] = val_exponentiated;
            val_exponentiated = field_mul_log(field, val_exponentiated, val_log);
        }
    }
}

polynomial_t polynomial_init_from_roots(field_t field, unsigned int nroots, field_element_t *roots, polynomial_t poly, polynomial_t *scratch) {
    unsigned int order = nroots;
    polynomial_t l;
    field_element_t l_coeff[2];
    l.order = 1;
    l.coeff = l_coeff;

    polynomial_t r[2];
    r[0] = scratch[0];
    r[1] = scratch[1];
    unsigned int rcoeffres = 0;

    r[rcoeffres].coeff[1] = 1;
    r[rcoeffres].coeff[0] = roots[0];
    r[rcoeffres].order = 1;

    l.coeff[1] = 1;

    for (unsigned int i = 1; i < nroots; i++) {
        l.coeff[0] = roots[i];
        unsigned int nextrcoeff = rcoeffres;
        rcoeffres = (rcoeffres + 1) % 2;
        r[rcoeffres].order = i + 1;
        polynomial_mul(field, l, r[nextrcoeff], r[rcoeffres]);
    }

    memcpy(poly.coeff, r[rcoeffres].coeff, (order + 1) * sizeof(field_element_t));
    poly.order = order;

    return poly;
}

polynomial_t polynomial_create_from_roots(field_t field, unsigned int nroots, field_element_t *roots) {
    polynomial_t poly = polynomial_create(nroots);
    unsigned int order = nroots;
    polynomial_t l;
    l.order = 1;
    l.coeff = calloc(2, sizeof(field_element_t));

    polynomial_t r[2];
    r[0].coeff = calloc(order + 1, sizeof(field_element_t));
    r[1].coeff = calloc(order + 1, sizeof(field_element_t));
    unsigned int rcoeffres = 0;

    r[rcoeffres].coeff[0] = roots[0];
    r[rcoeffres].coeff[1] = 1;
    r[rcoeffres].order = 1;

    l.coeff[1] = 1;

    for (unsigned int i = 1; i < nroots; i++) {
        l.coeff[0] = roots[i];
        unsigned int nextrcoeff = rcoeffres;
        rcoeffres = (rcoeffres + 1) % 2;
        r[rcoeffres].order = i + 1;
        polynomial_mul(field, l, r[nextrcoeff], r[rcoeffres]);
    }

    memcpy(poly.coeff, r[rcoeffres].coeff, (order + 1) * sizeof(field_element_t));
    poly.order = order;

    free(l.coeff);
    free(r[0].coeff);
    free(r[1].coeff);

    return poly;
}
