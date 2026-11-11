/* Hash kernels (neura_hash.h). */
#include "neura_hash.h"

static uint64_t f64_bits(double x) {
    union {
        double f;
        uint64_t u;
    } pun;
    pun.f = x;
    return pun.u;
}

static uint32_t fold64(uint64_t v) { return (uint32_t)(v ^ (v >> 32)); }

static uint32_t f64_hash(double x) {
    /* Every NaN hashes as the canonical NaN. */
    return fold64(x != x ? UINT64_C(0x7FF8000000000000) : f64_bits(x));
}

neura_u32 neura_k_hash_string(neura_text s) {
    uint32_t h = 0;
    for (uint64_t i = 0; i < s.len; i++) {
        h = h * 31u + s.data[i];
    }
    return h;
}

neura_u32 neura_k_hash_combine(neura_u32 seed, neura_u32 value) { return seed * 31u + value; }

neura_u32 neura_k_hash_spread(neura_u32 hash) { return hash ^ (hash >> 16); }

neura_u32 neura_k_hash_i64(neura_i64 value) { return fold64((uint64_t)value); }

neura_u32 neura_k_hash_u64(neura_u64 value) { return fold64(value); }

neura_u32 neura_k_hash_f64(neura_f64 value) { return f64_hash(value); }

neura_u32 neura_k_hash_list_u8(neura_view_u8 items) {
    uint32_t h = 1;
    for (uint64_t i = 0; i < items.len; i++) {
        h = h * 31u + items.data[i];
    }
    return h;
}

neura_u32 neura_k_hash_list_i64(neura_view_i64 items) {
    uint32_t h = 1;
    for (uint64_t i = 0; i < items.len; i++) {
        h = h * 31u + fold64((uint64_t)items.data[i]);
    }
    return h;
}

neura_u32 neura_k_hash_list_u64(neura_view_u64 items) {
    uint32_t h = 1;
    for (uint64_t i = 0; i < items.len; i++) {
        h = h * 31u + fold64(items.data[i]);
    }
    return h;
}

neura_u32 neura_k_hash_list_f64(neura_view_f64 items) {
    uint32_t h = 1;
    for (uint64_t i = 0; i < items.len; i++) {
        h = h * 31u + f64_hash(items.data[i]);
    }
    return h;
}
