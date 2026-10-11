%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-OTP oracle only. This module is NOT loaded into libbeam; its binary
%% construction instructions deliberately exceed the admitted execution profile.
-module(bit_writer_reference).
-export([run/0, segments/0, aliases/0]).

run() ->
    Values = [0, -1, (1 bsl 59)-1, -(1 bsl 59), 1 bsl 64, -((1 bsl 64)+1)],
    Widths = [0,1,7,8,9,63,64,65,127,128,129,257],
    Offsets = [0,1,7,8,15],
    [integer(V, N, O, E) || V <- Values, N <- Widths, O <- Offsets, E <- [big,little]],
    Scalars = [0,16#7f,16#80,16#7ff,16#800,16#d7ff,16#e000,16#ffff,
               16#10000,16#10ffff,-1,16#d800,16#dfff,16#110000],
    [unicode(C, O, E) || C <- Scalars, O <- Offsets,
                        E <- [utf8,utf16_big,utf16_little,utf32_big,utf32_little]],
    ok.

segments() ->
    Raw = [0,16#8000000000000000,16#3ff0000000000000,16#3ff0020000000001,
           16#bff0020000000001,1,16#7fefffffffffffff,16#40effe0000000000,16#40effdffffffffff],
    Values = [from_raw(R) || R <- Raw] ++
        [(1 bsl 58)+(1 bsl 34)+1, (1 bsl 60)+(1 bsl 36)+1, 1 bsl 1024, [], -1],
    [float_field(V, W, O, E) || V <- Values, W <- [0,8,16,32,64,128],
                              O <- [0,1,7,8,15], E <- [big,little]],
    ok.

aliases() ->
    Data = << <<(16#a5 + I*73):8>> || I <- lists:seq(0,32) >>,
    [begin
         <<Prefix:N/bitstring, _/bitstring>> = Data,
         Bits = case Mode of
             0 -> <<Prefix/bitstring, Prefix/bitstring>>;
             1 -> <<Prefix/bitstring, 0:1, Prefix/bitstring>>
         end,
         Padding = (8 - bit_size(Bits) rem 8) rem 8,
         Binary = <<Bits/bitstring, 0:Padding>>,
         io:format("~B:~B:", [Mode,N]),
         io:put_chars([[io_lib:format("~2.16.0b", [B]) || <<B>> <= Binary], $\n])
     end || Mode <- [0,1], N <- lists:seq(1,257)],
    ok.

from_raw(Raw) -> <<F:64/float>> = <<Raw:64>>, F.

float_field(V, W, O, E) ->
    Prefix = 16#a5a5 bsr (16-O),
    try
        Bits = case E of
            big -> <<Prefix:O, V:W/float-big>>;
            little -> <<Prefix:O, V:W/float-little>>
        end,
        print(Bits)
    catch error:badarg -> io:put_chars("badarg\n")
    end.

integer(V, N, O, E) ->
    Prefix = 16#a5a5 bsr (16-O),
    Bits = case E of
        big -> <<Prefix:O, V:N/big-integer>>;
        little -> <<Prefix:O, V:N/little-integer>>
    end,
    print(Bits).

unicode(C, O, E) ->
    Prefix = 16#a5a5 bsr (16-O),
    try
        Bits = case E of
            utf8 -> <<Prefix:O, C/utf8>>;
            utf16_big -> <<Prefix:O, C/utf16-big>>;
            utf16_little -> <<Prefix:O, C/utf16-little>>;
            utf32_big -> <<Prefix:O, C/utf32-big>>;
            utf32_little -> <<Prefix:O, C/utf32-little>>
        end,
        print(Bits)
    catch error:badarg -> io:put_chars("badarg\n")
    end.

print(Bits) ->
    Padding = (8 - bit_size(Bits) rem 8) rem 8,
    Tail = 16#a5 band ((1 bsl Padding)-1),
    Binary = <<Bits/bitstring, Tail:Padding>>,
    io:put_chars([[io_lib:format("~2.16.0b", [B]) || <<B>> <= Binary], $\n]).
