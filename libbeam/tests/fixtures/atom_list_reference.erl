%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(atom_list_reference).
-export([run/0]).
run() ->
    Vectors = [[],[0],[97,0,98],"hello",[16#7f,16#80,16#ff,16#100,16#7ff,16#800],
               [16#d7ff,16#e000,16#ffff,16#10000,16#1f600,16#10ffff],lists:duplicate(255,16#10ffff)],
    lists:foreach(fun(Chars) ->
        Atom = list_to_atom(Chars),
        Chars = atom_list_slice:convert(Atom),
        Atom = atom_list_slice:from_list(Chars),
        Atom = atom_list_slice:existing(Chars),
        Utf8 = unicode:characters_to_binary(Chars),
        Utf8 = atom_list_slice:utf8(Atom)
    end, Vectors),
    <<0,127,128,255>> = atom_list_slice:latin1(list_to_atom([0,127,128,255])),
    badarg(fun() -> atom_list_slice:latin1(list_to_atom([256])) end),
    lists:foreach(fun(Value) -> badarg(fun() -> atom_list_slice:convert(Value) end) end,
        [[],0,1.0,<<"atom">>,[97],{atom}]),
    lists:foreach(fun(Value) ->
        badarg(fun() -> atom_list_slice:from_list(Value) end),
        badarg(fun() -> atom_list_slice:existing(Value) end)
    end, [7,[false],[-1],[16#d800],[16#110000],[1.0],[1 bsl 64],
          lists:duplicate(255,97) ++ false, lists:duplicate(254,97) ++ [false,97]]),
    lists:foreach(fun(Value) ->
        failure(fun() -> atom_list_slice:from_list(Value) end, system_limit),
        badarg(fun() -> atom_list_slice:existing(Value) end)
    end, [lists:duplicate(256,97), lists:duplicate(255,97) ++ [false]]),
    Fresh = [16#10ffff,108,105,115,116,0,113],
    badarg(fun() -> atom_list_slice:existing(Fresh) end),
    Created = atom_list_slice:from_list(Fresh),
    Created = atom_list_slice:existing(Fresh),
    Fresh = atom_to_list(Created),
    <<"payload">> = atom_list_slice:copy(<<"payload">>),
    io:format("OTP_ATOM_LIST_REFERENCE_OK unicode_scalars=true nul_empty=true max_length=true binary_regression=true~n").
badarg(F) -> failure(F,badarg).
failure(F,Reason) ->
    try F() of _ -> error(expected_failure)
    catch error:Reason -> ok end.
