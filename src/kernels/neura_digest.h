/* Digest kernels: SHA-256 (FIPS 180-4) and CRC-32 (IEEE 802.3). They
 * replace the bodies of stdlib/std/digest/sha256.nra and crc32.nra.
 *
 * The SHA-256 state is NEURA_SHA256_STATE_SIZE bytes, little-endian:
 *   [0, 32)    the eight hash words
 *   [32, 40)   the number of message bytes added so far
 *   [40, 104)  the bytes of an incomplete block, zero past those in use
 *   104        how many of those bytes are in use, 0 through 63
 *   105        1 once the digest is finished, otherwise 0
 *   [106, 112) zero
 * sha256_init writes a fresh state. A state of another length, or one that
 * breaks a rule above, gives NEURA_TRAP_INVALID_STATE. Updating or finishing a finished state gives
 * NEURA_TRAP_DIGEST_FINISHED, as Sha256.update and Sha256.finish trap. */
#ifndef NEURA_DIGEST_H
#define NEURA_DIGEST_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEURA_SHA256_STATE_SIZE 112

/* Sha256.of: the 32-byte hash of the bytes. */
NEURA_KERNEL("neura.digest.sha256", 1, "1 + bytes / 64")
NEURA_KERNEL_BUFFER("out", "32")
neura_status neura_k_digest_sha256(neura_bytes bytes, neura_out_bytes* out);

/* new Sha256(): a fresh running digest. */
NEURA_KERNEL("neura.digest.sha256_init", 1, "1")
NEURA_KERNEL_BUFFER("state", "112")
neura_status neura_k_digest_sha256_init(neura_state* state);

/* Sha256.update. */
NEURA_KERNEL("neura.digest.sha256_update", 1, "1 + bytes / 64")
NEURA_KERNEL_BUFFER("state", "112")
neura_status neura_k_digest_sha256_update(neura_state* state, neura_bytes bytes);

/* Sha256.finish: pads, ends the digest, and writes the 32-byte hash. */
NEURA_KERNEL("neura.digest.sha256_finish", 1, "2")
NEURA_KERNEL_BUFFER("state", "112")
NEURA_KERNEL_BUFFER("out", "32")
neura_status neura_k_digest_sha256_finish(neura_state* state, neura_out_bytes* out);

/* Crc32.of: the checksum of the bytes. */
NEURA_KERNEL("neura.digest.crc32", 1, "1 + bytes / 64")
neura_u32 neura_k_digest_crc32(neura_bytes bytes);

/* Crc32.update on the running register, which a Crc32 keeps without the
 * final exclusive-or (initially 0xFFFFFFFF; value() is register ^
 * 0xFFFFFFFF). Returns the advanced register. */
NEURA_KERNEL("neura.digest.crc32_update", 1, "1 + bytes / 64")
neura_u32 neura_k_digest_crc32_update(neura_u32 register_value, neura_bytes bytes);

#ifdef __cplusplus
}
#endif

#endif
