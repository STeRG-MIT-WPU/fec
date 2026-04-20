#include "correct/convolutional/metric.h"

// measure the square of the euclidean distance between x and y
distance_t metric_soft_distance_quadratic(unsigned int hard_x, const uint8_t *soft_y, size_t len) {
    distance_t dist = 0;
    for (unsigned int i = 0; i < len; i++) {
        unsigned int soft_x = (hard_x & 1) ? 255 : 0;
        hard_x >>= 1;
        int d = soft_y[i] - soft_x;
        dist += d*d;
    }
    return dist >> 3;
}
