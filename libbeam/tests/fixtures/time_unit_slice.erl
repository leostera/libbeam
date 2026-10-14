%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(time_unit_slice).
-export([identity/1,copy/1,all/1,units/0,times/0,invoke/4,bad_time/1,bad_from/1,bad_to/1]).
identity(B) -> B.
units() -> [second,millisecond,microsecond,nanosecond,native,perf_counter,
            seconds,milli_seconds,micro_seconds,nano_seconds,3,7,1180591620717411303427].
times() -> [0,1,-1,2,-2,999,-999,-576460752303423488,576460752303423487,
            18446744073709551616,-18446744073709551616,
            170141183460469231731687303715884105728,-170141183460469231731687303715884105728,
            13407807929942597099574024998205846127479365820592393377723561443721764030073546976801874298166903427690031858186486050853753882811946569946433649006084096,
            -13407807929942597099574024998205846127479365820592393377723561443721764030073546976801874298166903427690031858186486050853753882811946569946433649006084096].
nth(1,[H|_]) -> H;
nth(N,[_|T]) -> nth(N-1,T).
invoke(F,T,From,To) -> apply(erlang,F,[T,From,To]).
all(B) -> From=nth(binary_to_integer(B),units()), iolist_to_binary(tos(units(),From)).
tos([],_) -> [];
tos([To|Rest],From) -> [rows(times(),From,To)|tos(Rest,From)].
rows([],_,_) -> [];
rows([T|Rest],From,To) ->
    R=erlang:convert_time_unit(T,From,To), R=invoke(convert_time_unit,T,From,To),
    [integer_to_binary(T),<<":">>,integer_to_binary(R),<<"\n">>|rows(Rest,From,To)].
copy(B) ->
    -1=erlang:convert_time_unit(-1,nanosecond,second),
    -2=invoke(convert_time_unit,-1001,millisecond,second),
    1000000000=invoke(convert_time_unit,1,second,native),
    1=invoke(convert_time_unit,1,perf_counter,nanosecond),
    true=erlang:garbage_collect(), B.
bad_time(B) -> erlang:convert_time_unit(B,second,millisecond).
bad_from(B) -> erlang:convert_time_unit(1,B,second).
bad_to(B) -> erlang:convert_time_unit(1,second,B).
