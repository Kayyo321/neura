#!/usr/bin/env python3
"""Finds divisions that exercise the corrections of Knuth's Algorithm D as
std/math/big-integer.nra implements it (knuth_divide, mirrored below step for
step): qhat corrected in D3 and the add-back of D6. generate.py lists the
cases it printed. Usage: python3 -I knuth-search.py
"""
import random
B = 1 << 32
def limbs(x):
    out = []
    while x: out.append(x & (B - 1)); x >>= 32
    return out
def value(l): return sum(v << (32 * i) for i, v in enumerate(l))
def knuth(a, b):
    """Mirror of big-integer.nra knuth_divide; returns (q, r, corrections, addbacks)."""
    n = len(b); m = len(a) - n
    s = 32 - b[-1].bit_length()
    v = limbs(value(b) << s); u = limbs(value(a) << s)
    while len(u) < len(a) + 1: u.append(0)
    top, nxt = v[n - 1], v[n - 2]
    q = [0] * (m + 1); corr = 0; addb = 0
    for j in range(m, -1, -1):
        num = (u[j + n] << 32) | u[j + n - 1]
        qhat, rhat = divmod(num, top)
        while qhat >= B or qhat * nxt > ((rhat << 32) | u[j + n - 2]):
            qhat -= 1; rhat += top; corr += 1
            if rhat >= B: break
        borrow = carry = 0
        for i in range(n):
            p = qhat * v[i] + carry; carry = p >> 32
            low = (p & (B - 1)) + borrow
            x = u[i + j]
            if x >= low: u[i + j] = x - low; borrow = 0
            else: u[i + j] = x + B - low; borrow = 1
        x = u[j + n]; needed = carry + borrow
        neg = x < needed
        u[j + n] = (x + B - needed) if neg else (x - needed)
        if neg:
            addb += 1; qhat -= 1; back = 0
            for i in range(n):
                t = u[i + j] + v[i] + back; u[i + j] = t & (B - 1); back = t >> 32
            u[j + n] = (u[j + n] + back) & (B - 1)
        q[j] = qhat
    r = value(u[:n]) >> s
    return value(q), r, corr, addb
r = random.Random(7)
special = [0, 1, 2, 0x7fffffff, 0x80000000, 0x80000001, 0xffffffff, 0xfffffffe, 0x7ffffffe]
found_add = []; found_corr = []
tries = 0
while (len(found_add) < 6 or len(found_corr) < 6) and tries < 3000000:
    tries += 1
    nb = r.randint(2, 4); na = nb + r.randint(0, 3)
    pick = lambda: r.choice(special) if r.random() < 0.85 else r.getrandbits(32)
    b = [pick() for _ in range(nb)]; a = [pick() for _ in range(na)]
    if b[-1] == 0 or a[-1] == 0: continue
    A, Bv = value(a), value(b)
    if A < Bv: continue
    q, rr, c, ab = knuth(a, b)
    assert (q, rr) == divmod(A, Bv), (a, b)
    if ab and len(found_add) < 6: found_add.append((A, Bv))
    if c >= 2 and len(found_corr) < 6: found_corr.append((A, Bv))
print("tries", tries)
for A, Bv in found_add: print("ADD", hex(A), hex(Bv), knuth(limbs(A), limbs(Bv))[2:])
for A, Bv in found_corr: print("CORR", hex(A), hex(Bv), knuth(limbs(A), limbs(Bv))[2:])
