/* Sorting kernels: Sorting.sort (stdlib/std/collections/sorting.nra) over
 * int64, uint64, and float64 arrays with the companion comparators
 * Int64.compare, UInt64.compare, and Float64.total_compare.
 *
 * The algorithm is the Neura one: binary insertion sort of blocks of 16,
 * then bottom-up merging that copies a pair of runs already in order and
 * otherwise takes from the right run only when it is strictly less. It is
 * stable, so the result is the only stable order of the comparator: for
 * float64, -Infinity, negative values, -0.0, 0.0, positive values,
 * Infinity, then every NaN in its original order with its bit pattern
 * kept.
 *
 * `scratch` must hold at least as many elements as `items`, otherwise the
 * result is NEURA_TRAP_SCRATCH_TOO_SMALL and `items` is unchanged; its
 * contents afterwards are unspecified. A wrapper sorting a range passes the
 * range as `items`. */
#ifndef NEURA_SORT_H
#define NEURA_SORT_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sorting.sort of an int64 list with Int64.compare. */
NEURA_KERNEL("neura.sort.i64", 1, "1 + items * (1 + log2(items)) / 8")
NEURA_KERNEL_BUFFER("scratch", "items")
neura_status neura_k_sort_i64(neura_mut_i64 items, neura_mut_i64 scratch);

/* Sorting.sort of a uint64 list with UInt64.compare. */
NEURA_KERNEL("neura.sort.u64", 1, "1 + items * (1 + log2(items)) / 8")
NEURA_KERNEL_BUFFER("scratch", "items")
neura_status neura_k_sort_u64(neura_mut_u64 items, neura_mut_u64 scratch);

/* Sorting.sort of a float64 list with Float64.total_compare. */
NEURA_KERNEL("neura.sort.f64", 1, "1 + items * (1 + log2(items)) / 8")
NEURA_KERNEL_BUFFER("scratch", "items")
neura_status neura_k_sort_f64(neura_mut_f64 items, neura_mut_f64 scratch);

#ifdef __cplusplus
}
#endif

#endif
