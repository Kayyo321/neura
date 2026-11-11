/* Sorting kernels (neura_sort.h): Sorting.sort_range of
 * stdlib/std/collections/sorting.nra, instantiated per element type. A
 * comparison returns <0, 0, or >0 for Less, Equal, and Greater. */
#include "neura_sort.h"

#define NEURA_SORT_BLOCK 16u

static int compare_i64(int64_t a, int64_t b) { return (a > b) - (a < b); }

static int compare_u64(uint64_t a, uint64_t b) { return (a > b) - (a < b); }

static int sign_bit(double x) {
    union {
        double f;
        uint64_t u;
    } pun;
    pun.f = x;
    return (int)(pun.u >> 63);
}

/* Float64.total_compare. */
static int compare_f64(double a, double b) {
    int a_nan = a != a;
    int b_nan = b != b;
    if (a_nan || b_nan) {
        if (a_nan && b_nan) {
            return 0;
        }
        return a_nan ? 1 : -1;
    }
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    int a_negative = sign_bit(a);
    if (a_negative == sign_bit(b)) {
        return 0;
    }
    return a_negative ? -1 : 1;
}

#define NEURA_DEFINE_SORT(suffix, type)                                                                    \
    /* Sorting.insertion_sort over items[start, end). */                                                   \
    static void insertion_sort_##suffix(type* items, uint64_t start, uint64_t end) {                       \
        for (uint64_t i = start + 1; i < end; i++) {                                                       \
            type value = items[i];                                                                         \
            uint64_t low = start;                                                                          \
            uint64_t high = i;                                                                             \
            while (low < high) {                                                                           \
                uint64_t middle = low + (high - low) / 2;                                                  \
                if (compare_##suffix(items[middle], value) > 0) {                                          \
                    high = middle;                                                                         \
                } else {                                                                                   \
                    low = middle + 1;                                                                      \
                }                                                                                          \
            }                                                                                              \
            for (uint64_t k = i; k > low; k--) {                                                           \
                items[k] = items[k - 1];                                                                   \
            }                                                                                              \
            items[low] = value;                                                                            \
        }                                                                                                  \
    }                                                                                                      \
                                                                                                           \
    /* Sorting.merge_pass: merges adjacent runs of `width` from source into destination. */                \
    static void merge_pass_##suffix(const type* source, type* destination, uint64_t count, uint64_t width) { \
        uint64_t left = 0;                                                                                 \
        while (left < count) {                                                                             \
            uint64_t middle = width > count - left ? count : left + width;                                 \
            uint64_t right_end = width > count - middle ? count : middle + width;                          \
            int copy_only = middle == right_end;                                                           \
            if (!copy_only) {                                                                              \
                copy_only = compare_##suffix(source[middle - 1], source[middle]) <= 0;                     \
            }                                                                                              \
            if (copy_only) {                                                                               \
                for (uint64_t k = left; k < right_end; k++) {                                              \
                    destination[k] = source[k];                                                            \
                }                                                                                          \
            } else {                                                                                       \
                uint64_t i = left;                                                                         \
                uint64_t j = middle;                                                                       \
                uint64_t k = left;                                                                         \
                while (i < middle && j < right_end) {                                                      \
                    if (compare_##suffix(source[j], source[i]) < 0) {                                      \
                        destination[k++] = source[j++];                                                    \
                    } else {                                                                               \
                        destination[k++] = source[i++];                                                    \
                    }                                                                                      \
                }                                                                                          \
                while (i < middle) {                                                                       \
                    destination[k++] = source[i++];                                                        \
                }                                                                                          \
                while (j < right_end) {                                                                    \
                    destination[k++] = source[j++];                                                        \
                }                                                                                          \
            }                                                                                              \
            left = right_end;                                                                              \
        }                                                                                                  \
    }                                                                                                      \
                                                                                                           \
    neura_status neura_k_sort_##suffix(neura_mut_##suffix items, neura_mut_##suffix scratch) {             \
        if (scratch.len < items.len) {                                                                     \
            return NEURA_TRAP_SCRATCH_TOO_SMALL;                                                           \
        }                                                                                                  \
        uint64_t count = items.len;                                                                        \
        for (uint64_t block = 0; block < count; block += NEURA_SORT_BLOCK) {                               \
            uint64_t block_end = count - block > NEURA_SORT_BLOCK ? block + NEURA_SORT_BLOCK : count;      \
            insertion_sort_##suffix(items.data, block, block_end);                                         \
        }                                                                                                  \
        if (count <= NEURA_SORT_BLOCK) {                                                                   \
            return NEURA_OK;                                                                               \
        }                                                                                                  \
        /* Sorting.merge_blocks. */                                                                        \
        for (uint64_t k = 0; k < count; k++) {                                                             \
            scratch.data[k] = items.data[k];                                                               \
        }                                                                                                  \
        int in_buffer = 0;                                                                                 \
        uint64_t width = NEURA_SORT_BLOCK;                                                                 \
        while (width < count) {                                                                            \
            if (in_buffer) {                                                                               \
                merge_pass_##suffix(scratch.data, items.data, count, width);                               \
            } else {                                                                                       \
                merge_pass_##suffix(items.data, scratch.data, count, width);                               \
            }                                                                                              \
            in_buffer = !in_buffer;                                                                        \
            width = width > count / 2 ? count : width * 2;                                                 \
        }                                                                                                  \
        if (in_buffer) {                                                                                   \
            for (uint64_t k = 0; k < count; k++) {                                                         \
                items.data[k] = scratch.data[k];                                                           \
            }                                                                                              \
        }                                                                                                  \
        return NEURA_OK;                                                                                   \
    }

NEURA_DEFINE_SORT(i64, int64_t)
NEURA_DEFINE_SORT(u64, uint64_t)
NEURA_DEFINE_SORT(f64, double)
