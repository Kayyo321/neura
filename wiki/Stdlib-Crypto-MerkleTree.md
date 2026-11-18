# `Std.Crypto.Hash.MerkleTree`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Hash.MerkleTree`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Hash.MerkleTree`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MerkleTree** · [`export let leaf_hash = func<H: StreamingDigest<H>>(data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L27)
  H(0x00 || data). Tagged `#NoDiscard`. Since 0.3.0.
- **MerkleTree** · [`export let node_hash = func<H: StreamingDigest<H>>(left: uint8[list], right: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L31)
  H(0x01 || left || right). Tagged `#NoDiscard`. Since 0.3.0.
- **MerkleTree** · [`export let MerkleTree = class<H: StreamingDigest<H>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L59)
  An append-only tree of leaf hashes over the digest H. Tagged `#Final`. Since 0.3.0.
- **MerkleTree.MerkleTree** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L64)
  An empty tree. Checks the policy for MerkleTree (and, at the first hash, for H).
- **MerkleTree.MerkleTree** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L69)
  The number of leaves.
- **MerkleTree.MerkleTree** · [`export let append = func(data: uint8[list]) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L72)
  Appends a leaf with the data and returns its index.
- **MerkleTree.MerkleTree** · [`export let leaf = func(index: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L78)
  The leaf hash at the index.
- **MerkleTree.MerkleTree** · [`export let root = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L81)
  The root of the whole tree.
- **MerkleTree.MerkleTree** · [`export let root_at = func(tree_size: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L85)
  The root of the tree of the first `tree_size` leaves. Traps with "invalid argument: tree size" unless 0 <= tree_size <= size().
- **MerkleTree.MerkleTree** · [`export let inclusion_proof = func(index: int, tree_size: int) -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L92)
  The audit path of leaf `index` in the tree of the first `tree_size` leaves (RFC 6962 PATH). Traps unless index < tree_size <= size().
- **MerkleTree.MerkleTree** · [`export let consistency_proof = func(old_size: int, new_size: int) -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L105)
  The proof that the tree of `old_size` leaves is a prefix of the tree of `new_size` leaves (RFC 6962 PROOF). Empty when old_size is 0 or equals new_size. Traps unless 0 <= old_size <= new_size <= size().
- **MerkleTree.MerkleTree** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L158)
- **MerkleTree** · [`export let verify_inclusion = func<H: StreamingDigest<H>>(leaf: uint8[list], index: int, tree_size: int, proof: uint8[list][list], root: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L164)
  Whether `proof` shows that the leaf hash is at `index` in the tree of `tree_size` leaves with the root (RFC 9162, section 2.1.3.2). Tagged `#NoDiscard`. Since 0.3.0.
- **MerkleTree** · [`export let verify_consistency = func<H: StreamingDigest<H>>(old_size: int, new_size: int, old_root: uint8[list], new_root: uint8[list], proof: uint8[list][list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/merkle-tree.nra#L195)
  Whether `proof` shows that the tree of `old_size` leaves with `old_root` is a prefix of the tree of `new_size` leaves with `new_root` (RFC 9162, section 2.1.4.2). Equal sizes need an empty proof and equal roots; an old size of 0 needs an empty proof. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#204-stdcryptohash). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
