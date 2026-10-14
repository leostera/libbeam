%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(radix_slice).
-export([identity/1,copy/1,all/1,convert/3,unary/2,bad_digit/1,bad_base/1,bad_type/1,bad_bits/1,
         bad_integer/1,bad_integer_two/1,bad_integer_base/1,list_copy/1,list_head/1]).
identity(B) -> B.
convert(F,B,Base) -> apply(erlang,F,[B,Base]).
unary(F,N) -> apply(erlang,F,[N]).
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
    Digits = integer_to_list(Value,Base), Digits = convert(integer_to_list,Value,Base),
    Decimal = integer_to_list(Value), Decimal = unary(integer_to_list,Value),
    Encoded = integer_to_binary(Value,Base), Encoded = iolist_to_binary(Digits),
    DecimalBinary = integer_to_binary(Value), DecimalBinary = iolist_to_binary(Decimal),
    [integer_to_binary(Base),<<":">>,Text,<<":">>,DecimalBinary,<<":">>,Digits,<<":">>,Decimal,<<"\n">>].
digit(1,[Byte|_]) -> Byte;
digit(N,[_|Rest]) -> digit(N-1,Rest).
repeat(0,_) -> [];
repeat(N,Byte) -> [Byte|repeat(N-1,Byte)].
list_head(B) ->
    [Head|Tail] = integer_to_list(byte_size(B)),
    Encoded = integer_to_binary(byte_size(B)), Encoded = iolist_to_binary([Head|Tail]), B.
list_copy(B) ->
    B = list_head(B),
    "0" = integer_to_list(0), "-576460752303423488" = integer_to_list(-576460752303423488),
    "576460752303423487" = unary(integer_to_list,576460752303423487),
    "10000000000000000" = integer_to_list(18446744073709551616,16),
    "-10000000000000000" = convert(integer_to_list,-18446744073709551616,16),
    true = erlang:garbage_collect(), B.
bad_integer(B) -> integer_to_list(B).
bad_integer_two(B) -> integer_to_list(B,16).
bad_integer_base(B) -> integer_to_list(0,B).
bad_digit(B) -> binary_to_integer(<<B/binary,"_">>,36).
bad_base(B) -> binary_to_integer(B,37).
bad_type(B) -> binary_to_integer({B},10).
bad_bits(B) -> binary_to_integer(<<B/binary,1:1>>,2).
