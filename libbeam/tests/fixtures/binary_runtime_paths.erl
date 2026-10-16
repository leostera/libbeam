% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_runtime_paths).
-export([entry/1,identity/1,invoke/3,check/3]).

entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of
        true -> <<"ok">>;
        Failure when is_atom(Failure) -> atom_to_binary(Failure,utf8);
        _ -> <<"contract mismatch">>
    end.
identity(Input) -> Input.
invoke(_F,direct,[Seed]) -> runtime_paths(Seed);
invoke(F,_DynamicMode,Args) -> erlang:apply(?MODULE,F,Args).
check(F,Mode,Seed) ->
    try invoke(F,Mode,[Seed]) of ok -> true; Failure -> Failure catch _:_ -> unknown_exception end.

runtime_paths(_Seed) ->
    Cases=[{utf16,fun utf16_case/0},{float,fun float_case/0},
           {unicode,fun unicode_case/0},
           {unicode_retraction_utf8_invalid,fun unicode_retraction_utf8_invalid/0},
           {unicode_retraction_utf16_invalid,fun unicode_retraction_utf16_invalid/0},
           {unicode_retraction_utf32_invalid,fun unicode_retraction_utf32_invalid/0},
           {gc,fun gc_case/0},{perf_counter,fun counter_case/0},
           {nested_binary_tail,fun nested_binary_tail/0},
           {nested_binary_tail_accumulator,fun nested_binary_tail_accumulator/0},
           {lists_reverse,fun lists_reverse_case/0}],
    run_cases(Cases).
run_cases([]) -> ok;
run_cases([{Name,Fun}|Rest]) ->
    try Fun() of ok -> run_cases(Rest); Failure -> Failure catch _:_ -> Name end.
utf16_case() ->
    <<Emoji/utf16>> = <<16#1F642/utf16>>, 16#1F642 = Emoji,
    <<_Skipped16/utf16, Emoji/utf16>> = <<$A/utf16,16#1F642/utf16>>,
    <<Scalar/utf32>> = <<16#10FFFF/utf32>>, 16#10FFFF = Scalar,
    <<_Skipped32/utf32, Scalar/utf32>> = <<$A/utf32,16#10FFFF/utf32>>, ok.
float_case() ->
    try <<Float:32/float-big>> = <<1.5:32/float-big>> of
        _ when Float =/= 1.5 -> float_value;
        _ -> case expect_failure(fun() -> <<_:32/float>> = <<0:24>> end) of
            true -> ok;
            false -> float_boundary
        end
    catch _:_ -> float_extract_exception
    end.
unicode_case() ->
    true = expect_failure(fun() -> <<_/utf8>> = <<16#FF>> end),
    true = expect_failure(fun() -> <<_/utf16>> = <<16#D800:16>> end),
    true = expect_failure(fun() -> <<_/utf32>> = <<16#110000:32>> end),
    true = expect_failure(fun() -> <<_/utf16>> = <<0:8>> end),
    true = expect_failure(fun() -> <<_/utf32>> = <<0:24>> end), ok.
unicode_retraction_utf8_invalid() -> true = retract_utf8(<<16#FF>>), ok.
unicode_retraction_utf16_invalid() -> true = retract_utf16(<<16#D800:16>>), ok.
unicode_retraction_utf32_invalid() -> true = retract_utf32(<<16#110000:32>>), ok.
retract_utf8(<<_/utf8, 0>>) -> false;
retract_utf8(<<16#FF>>) -> true.
retract_utf16(<<_/utf16, 0>>) -> false;
retract_utf16(<<16#D800:16>>) -> true.
retract_utf32(<<_/utf32, 0>>) -> false;
retract_utf32(<<16#110000:32>>) -> true.
gc_case() ->
    Held = <<16#A5:8, 0:65528>>, true = erlang:garbage_collect(),
    8192 = byte_size(Held), <<16#A5, _/binary>> = Held, ok.
counter_case() ->
    Before = os:perf_counter(), true = is_integer(Before),
    true = erlang:garbage_collect(), After = os:perf_counter(),
    true = is_integer(After), true = After > Before, ok.
nested_binary_tail() ->
    Header = <<1,0,1,"x","v">>,
    true = parse_headers(3, <<Header/binary,Header/binary,Header/binary>>), ok.
parse_headers(0, <<>>) -> true;
parse_headers(N, <<NameSize:8,ValueSize:16,_Name:NameSize/binary,_Value:ValueSize/binary,Rest/binary>>) when N > 0 ->
    parse_headers(N-1, Rest).
nested_binary_tail_accumulator() ->
    Header = <<1,0,1,"x","v">>,
    3 = parse_headers_acc(3, <<Header/binary,Header/binary,Header/binary>>, []), ok.
parse_headers_acc(0, <<>>, Acc) -> erlang:length(Acc);
parse_headers_acc(N, <<NameSize:8,ValueSize:16,Name:NameSize/binary,Value:ValueSize/binary,Rest/binary>>, Acc) when N > 0 ->
    parse_headers_acc(N-1, Rest, [{Name,Value}|Acc]).
lists_reverse_case() ->
    [3,2,1] = lists:reverse([3],[2,1]),
    true = expect_failure(fun() -> lists:reverse([1|improper],[]) end), ok.
expect_failure(Fun) ->
    try Fun() of _ -> false catch _:_ -> true end.
