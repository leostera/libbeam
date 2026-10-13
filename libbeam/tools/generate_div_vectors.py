#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Deterministic independent integer-division oracle (Python arbitrary integers)."""
import pathlib
import random
import sys

rng = random.Random(0xBEA0D1)
base = 1 << 64
pairs = [(0, 1), (1, 1), (1, 2), (base, base - 1),
         ((1 << 59), 1), ((1 << 59), 3), (base * base - 1, base - 1)]
# Leading-digit equality, normalization extremes and low-limb add-back cases.
for divisor in [base + 1, base * base + base + 1, base**3 - 1,
                (base // 2) * base**2 + 1, base**4 + base**2 + 3]:
    for quotient in [1, 2, base - 1, base, base**2 + 1]:
        for remainder in [0, 1, divisor - 1]:
            pairs.append((quotient * divisor + remainder, divisor))
        pairs.append((quotient * divisor - 1, divisor))
for _ in range(256):
    divisor = rng.getrandbits(rng.randrange(1, 1025)) or 1
    quotient = rng.getrandbits(rng.randrange(0, 1025))
    remainder = rng.randrange(divisor)
    pairs.append((quotient * divisor + remainder, divisor))
lines = []
for left, right in pairs:
    quotient, remainder = divmod(left, right)
    assert left == quotient * right + remainder and 0 <= remainder < right
    lines.append(f'{left:x} {right:x} {quotient:x} {remainder:x}\n')
pathlib.Path(sys.argv[1]).write_text(''.join(lines))
