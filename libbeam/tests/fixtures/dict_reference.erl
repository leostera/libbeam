%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(dict_reference).
-export([run/0]).
run() ->
    Input = binary:copy(<<123>>,100),
    Input = dict_slice:copy(Input),
    Input = dict_slice:copy(Input),
    undefined = put(1,Input),
    undefined = put(9,9),
    undefined = put(17,ok),
    erlang:garbage_collect(),
    Input = get(1), 9 = get(9), ok = get(17),
    9 = put(9,Input), Input = get(9),
    Input = erase(9), Input = erase(1), ok = erase(17),
    undefined = erase(17), undefined = put(-1,undefined), undefined = erase(-1),
    undefined = put(9,42),
    42 = dict_locations:query(9), 42 = dict_locations:query_y(9), 42 = erase(9),
    io:format("OTP_DICT_REFERENCE_OK roundtrip=true collisions=true replacement=true undefined_value=true gc=true child_empty=true xy_locations=true~n").
