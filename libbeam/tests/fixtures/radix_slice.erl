%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(radix_slice).
-export([identity/1,copy/1,all/1,convert/3,bad_digit/1,bad_base/1,bad_type/1,bad_bits/1]).
identity(B) -> B.
convert(F,B,Base) -> apply(erlang,F,[B,Base]).
copy(B) ->
    18446744073709551616 = binary_to_integer(<<"10000000000000000">>,16),
    -18446744073709551616 = convert(binary_to_integer,<<"-10000000000000000">>,16),
    35 = binary_to_integer(<<"z">>,36), 35 = convert(binary_to_integer,<<"Z">>,36),
    true = erlang:garbage_collect(), B.
all(BaseText) ->
    Base = binary_to_integer(BaseText),
    Numbers = [0,1,255,256,576460752303423487,576460752303423488,
               576460752303423489,9223372036854775808,18446744073709551615,
               18446744073709551616,170141183460469231731687303715884105728,
               170141183460469231731687303715884105735],
    Wide = integer_to_binary(18446744073709551616,Base),
    Upper = digit(Base,"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
    Lower = digit(Base,"0123456789abcdefghijklmnopqrstuvwxyz"),
    Extra = [<<"+000">>,<<"-000">>,<<"0001">>,<<"+000",Wide/binary>>,<<"-000",Wide/binary>>,
             iolist_to_binary(repeat(257,Upper)),iolist_to_binary(repeat(257,Lower))],
    iolist_to_binary([numbers(Numbers,Base),strings(Extra,Base)]).
numbers([],_) -> [];
numbers([N|Ns],Base) ->
    [row(integer_to_binary(N,Base),Base),row(integer_to_binary(-N,Base),Base)|numbers(Ns,Base)].
strings([],_) -> [];
strings([Text|Rest],Base) -> [row(Text,Base)|strings(Rest,Base)].
row(Text,Base) ->
    Value = binary_to_integer(Text,Base), Value = convert(binary_to_integer,Text,Base),
    [integer_to_binary(Base),<<":">>,Text,<<":">>,integer_to_binary(Value),<<"\n">>].
digit(1,[Byte|_]) -> Byte;
digit(N,[_|Rest]) -> digit(N-1,Rest).
repeat(0,_) -> [];
repeat(N,Byte) -> [Byte|repeat(N-1,Byte)].
bad_digit(B) -> binary_to_integer(<<B/binary,"_">>,36).
bad_base(B) -> binary_to_integer(B,37).
bad_type(B) -> binary_to_integer({B},10).
bad_bits(B) -> binary_to_integer(<<B/binary,1:1>>,2).
