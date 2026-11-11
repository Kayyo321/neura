/* Hash kernels: the value hashes of Stdlib.md section 5.2 and the mixing
 * functions of Std.Core.Hash (stdlib/std/core/hash.nra) that the hash
 * collections use (stdlib/std/collections/hash-map.nra applies `spread`
 * before masking; stdlib/std/collections/internal/hash-core.nra only masks
 * and probes, so it has no kernel). */
#ifndef NEURA_HASH_H
#define NEURA_HASH_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* string.hash_code and Strings.hash: start with 0; for each byte, 31 times
 * the hash plus the byte, wrapping. */
NEURA_KERNEL("neura.hash.string", 1, "1 + s / 64")
neura_u32 neura_k_hash_string(neura_text s);

/* Hash.combine: 31 * seed + value, wrapping. */
NEURA_KERNEL("neura.hash.combine", 1, "1")
neura_u32 neura_k_hash_combine(neura_u32 seed, neura_u32 value);

/* Hash.spread: hash ^ (hash >> 16). */
NEURA_KERNEL("neura.hash.spread", 1, "1")
neura_u32 neura_k_hash_spread(neura_u32 hash);

/* Hash.of_int64 and int64.hash_code: low 32 bits ^ high 32 bits. */
NEURA_KERNEL("neura.hash.i64", 1, "1")
neura_u32 neura_k_hash_i64(neura_i64 value);

/* Hash.of_uint64 and uint64.hash_code. */
NEURA_KERNEL("neura.hash.u64", 1, "1")
neura_u32 neura_k_hash_u64(neura_u64 value);

/* float64.hash_code: the bit pattern with every NaN replaced by the
 * canonical NaN, folded like uint64. */
NEURA_KERNEL("neura.hash.f64", 1, "1")
neura_u32 neura_k_hash_f64(neura_f64 value);

/* hash_code of a uint8 array or list: start with 1; for each element, 31
 * times the hash plus the element. */
NEURA_KERNEL("neura.hash.list_u8", 1, "1 + items / 64")
neura_u32 neura_k_hash_list_u8(neura_view_u8 items);

/* hash_code of an int64 array or list, with the int64 element hash. */
NEURA_KERNEL("neura.hash.list_i64", 1, "1 + items / 16")
neura_u32 neura_k_hash_list_i64(neura_view_i64 items);

/* hash_code of a uint64 array or list. */
NEURA_KERNEL("neura.hash.list_u64", 1, "1 + items / 16")
neura_u32 neura_k_hash_list_u64(neura_view_u64 items);

/* hash_code of a float64 array or list. */
NEURA_KERNEL("neura.hash.list_f64", 1, "1 + items / 16")
neura_u32 neura_k_hash_list_f64(neura_view_f64 items);

#ifdef __cplusplus
}
#endif

#endif
