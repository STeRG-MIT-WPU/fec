/*
 * STM32 portable helpers.
 *
 * arm-none-eabi-gcc defines __GNUC__ so we get __builtin_popcount/prefetch.
 * For Keil (armcc) and IAR we fall back to a portable bit-trick popcount
 * and a no-op prefetch.
 */
#ifndef CORRECT_PORTABLE_H
#define CORRECT_PORTABLE_H

#if defined(__GNUC__) || defined(__clang__)
#define HAVE_BUILTINS
#endif


#ifdef HAVE_BUILTINS
#define popcount __builtin_popcount
#define prefetch __builtin_prefetch
#else

static inline int popcount(int x) {
    /* taken from http://graphics.stanford.edu/~seander/bithacks.html */
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    /* precedence fix: original libcorrect has a paren bug here */
    return (((x + (x >> 4)) & 0x0f0f0f0f) * 0x01010101) >> 24;
}

static inline void prefetch(const void *x) { (void)x; }

#endif

#endif /* CORRECT_PORTABLE_H */
