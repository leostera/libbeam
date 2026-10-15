%% SPDX-License-Identifier: Apache-2.0
%% Dynamic bs_skip_bits2 after an OTP-compiled binary match.
-module(skip_bits_slice).
-export([run/1, unaligned/1]).

run(<<Count:8, Bits/bits>>) ->
    <<_:Count/bits, Tail/bits>> = Bits,
    Tail.

unaligned(<<_:1, Count:8, Bits/bits>>) ->
    <<_:Count/bits, Tail/bits>> = Bits,
    Tail.
