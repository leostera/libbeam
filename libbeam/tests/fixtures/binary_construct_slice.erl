%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_construct_slice).
-export([integer/2, floating/2, binary/2, utf8/1, utf16/1, utf32/1,
         mixed/5, decorate/1, append/1, tuple/1, sized/2, first/1, ordered/2, literal/1]).
integer(I,N) -> <<I:N/little>>.
floating(V,N) -> <<V:N/little-float>>.
binary(B,N) -> <<B:N/bitstring>>.
utf8(V) -> <<V/utf8>>.
utf16(V) -> <<V/utf16-little>>.
utf32(V) -> <<V/utf32-little>>.
mixed(A,B,C,D,E) -> <<A:5,B:16/little-float,C/utf8,D/bitstring,E:13/little>>.
decorate(B) -> <<"owned:",B/binary,"!">>.
append(B) -> <<B/binary,33>>.
tuple(B) -> {<<"copy:",B/binary>>,B}.
sized(B,N) -> <<B:N/binary>>.
first(B) -> <<B:9/bitstring>>.
ordered(V,N) -> <<V/utf8,0:N>>.
literal(N) -> <<(1 bsl 64):N/little>>.
