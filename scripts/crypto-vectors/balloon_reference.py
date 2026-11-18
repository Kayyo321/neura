#!/usr/bin/env python3
"""An independent Python reference of Balloon hashing, for Std.Crypto.Kdf.Balloon.

No standard test vectors exist for Balloon hashing. This follows the
pseudocode of Algorithm 1 ("Balloon") in Boneh, Corrigan-Gibbs and Schechter,
"Balloon Hashing: A Memory-Hard Function Providing Provable Protection Against
Sequential Attacks" (ASIACRYPT 2016; https://eprint.iacr.org/2016/027),
with delta = 3, over a hashlib digest. The paper leaves the encoding of the
integers open; this reference and the Neura module both fix it as:

* every integer (the counter cnt, and t, m, i in ints_to_block) is 8 bytes,
  little-endian;
* a hash call hash(a, b, ...) hashes the concatenation of its arguments;
* to_int of a digest is its first 8 bytes read little-endian, reduced modulo
  s_cost.

So the outputs agree with other Balloon implementations only if they make the
same choices. The paper's algorithm:

    buf[0] = hash(cnt++, passwd, salt)
    for m in 1 .. s_cost-1: buf[m] = hash(cnt++, buf[m-1])
    for t in 0 .. t_cost-1:
      for m in 0 .. s_cost-1:
        prev = buf[(m-1) mod s_cost]
        buf[m] = hash(cnt++, prev, buf[m])
        for i in 0 .. delta-1:
          other = to_int(hash(cnt++, salt, ints_to_block(t, m, i))) mod s_cost
          buf[m] = hash(cnt++, buf[m], buf[other])
    return buf[s_cost-1]
"""

import hashlib

DELTA = 3


def le64(value: int) -> bytes:
    return value.to_bytes(8, "little")


def balloon(password: bytes, salt: bytes, space_cost: int, time_cost: int, name: str = "sha256") -> bytes:
    def h(*parts: bytes) -> bytes:
        digest = hashlib.new(name)
        for part in parts:
            digest.update(part)
        return digest.digest()

    count = 0
    buf = []
    buf.append(h(le64(count), password, salt))
    count += 1
    for m in range(1, space_cost):
        buf.append(h(le64(count), buf[m - 1]))
        count += 1
    for t in range(time_cost):
        for m in range(space_cost):
            previous = buf[(m - 1) % space_cost]
            buf[m] = h(le64(count), previous, buf[m])
            count += 1
            for i in range(DELTA):
                block = le64(t) + le64(m) + le64(i)
                other = int.from_bytes(h(le64(count), salt, block)[:8], "little") % space_cost
                count += 1
                buf[m] = h(le64(count), buf[m], buf[other])
                count += 1
    return buf[space_cost - 1]
