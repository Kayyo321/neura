#include "mathops.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int mathops_counter = 0;

int add(int a, int b) { return a + b; }
long long add64(long long a, long long b) { return a + b; }
double scale(double value, float factor) { return value * factor; }
unsigned char low_byte(unsigned int value) { return (unsigned char)(value & 0xff); }
signed char negate8(signed char value) { return (signed char)-value; }
_Bool is_even(int value) { return value % 2 == 0; }

mathops_point make_point(int x, int y) {
    mathops_point point = {x, y};
    return point;
}

int point_sum(mathops_point point) { return point.x + point.y; }

mathops_mixed make_mixed(signed char tag, double weight, short count) {
    mathops_mixed value = {tag, weight, count};
    return value;
}

double mixed_weight(mathops_mixed value) { return value.weight * value.count + value.tag; }

void divide(int a, int b, int* quotient, int* remainder) {
    *quotient = a / b;
    *remainder = a % b;
}

int string_length(const char* text) { return (int)strlen(text); }

const char* greeting(void) { return "hello from C"; }

int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; ++i) total += va_arg(args, int);
    va_end(args);
    return total;
}

int bump_counter(void) { return ++mathops_counter; }

void sort_ints(int* values, int count, mathops_compare compare) {
    qsort(values, (size_t)count, sizeof(int), compare);
}
