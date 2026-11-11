/* A small C library for the foreign-interop tests (ForeignInterop.md). */
#ifndef NEURA_TEST_MATHOPS_H
#define NEURA_TEST_MATHOPS_H

#include <stdint.h>

#define MATHOPS_VERSION 3
#define MATHOPS_LIMIT (1 << 20)
#define MATHOPS_SCALE 2.5
#define MATHOPS_NAME "mathops"
#define MATHOPS_TWICE(x) ((x) * 2)

enum mathops_color { MATHOPS_RED = 1, MATHOPS_GREEN = 4, MATHOPS_BLUE = -2 };

typedef struct mathops_point {
    int32_t x;
    int32_t y;
} mathops_point;

typedef struct mathops_mixed {
    signed char tag;
    double weight;
    int16_t count;
} mathops_mixed;

struct mathops_flags {
    unsigned ready : 1;
    unsigned level : 3;
};

typedef int (*mathops_compare)(const void*, const void*);

extern int mathops_counter;

int add(int a, int b);
long long add64(long long a, long long b);
double scale(double value, float factor);
unsigned char low_byte(unsigned int value);
signed char negate8(signed char value);
_Bool is_even(int value);
mathops_point make_point(int x, int y);
int point_sum(mathops_point point);
mathops_mixed make_mixed(signed char tag, double weight, short count);
double mixed_weight(mathops_mixed value);
void divide(int a, int b, int* quotient, int* remainder);
int string_length(const char* text);
const char* greeting(void);
int sum_ints(int count, ...);
int bump_counter(void);
void sort_ints(int* values, int count, mathops_compare compare);

static inline int mathops_square(int x) { return x * x; }

#endif
