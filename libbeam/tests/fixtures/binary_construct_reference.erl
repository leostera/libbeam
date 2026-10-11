%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-only driver; never loaded into the owned runtime.
-module(binary_construct_reference).
-export([run/0,ordering/0]).
emit(Tag,Bits) ->
    Pad = (8 - bit_size(Bits) rem 8) rem 8,
    Hex = binary:encode_hex(<<Bits/bitstring,0:Pad>>),
    io:format("~s:~B:~s~n", [Tag,bit_size(Bits),Hex]).
run() ->
    lists:foreach(fun(I) -> lists:foreach(fun(N) ->
        emit(io_lib:format("integer:~B:~B",[I,N]), binary_construct_slice:integer(I,N))
    end, lists:seq(0,129)) end, [-129,-1,0,1,511]),
    lists:foreach(fun(N) -> emit(io_lib:format("float:~B",[N]),binary_construct_slice:floating(1.1,N)) end,[16,32,64]),
    lists:foreach(fun(F) -> lists:foreach(fun(V) ->
        emit(io_lib:format("~s:~B",[F,V]),apply(binary_construct_slice,F,[V]))
    end,[0,127,128,2047,2048,16#1f642]) end,[utf8,utf16,utf32]),
    B = list_to_binary(lists:seq(0,64)),
    lists:foreach(fun(N) -> emit(io_lib:format("binary:~B",[N]),binary_construct_slice:binary(B,N)) end,[0,1,7,8,9,519,520]),
    lists:foreach(fun(F) -> emit(atom_to_list(F),apply(binary_construct_slice,F,[B])) end,[decorate,append,first]),
    emit("mixed",binary_construct_slice:mixed(19,3,16#1f642,B,-7)),
    emit("literal",binary_construct_slice:literal(72)).
ordering() ->
    try binary_construct_slice:ordered(false,-1) of
        _ -> error(expected_failure)
    catch error:badarg:Stack ->
        [{_,_,_,Extra}|_] = Stack,
        {Segment,Operation,Detail,_} = maps:get(cause,proplists:get_value(error_info,Extra)),
        io:format("~B:~w:~w~n",[Segment,Operation,Detail])
    end.
