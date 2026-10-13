%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(map_key_reference).
-export([run/0]).
run() ->
    true = map_key_slice:flat(1),
    false = map_key_slice:flat(99),
    false = map_key_slice:present(a, #{}),
    true = map_key_slice:large(33),
    false = map_key_slice:large(99),
    yes = map_key_slice:guard_large(1),
    no = map_key_slice:guard_large(99),
    no = map_key_slice:guarded(1, 123),
    %% Binary HAMT queries are valid in OTP but currently refused natively.
    false = map_key_slice:large(<<"key">>),
    no = map_key_slice:guard_large(<<"key">>),
    try map_key_slice:badmap(123) of _ -> error(expected_badmap)
    catch error:{badmap,123} -> ok end,
    Key = lists:foldl(fun(_, Acc) -> {Acc,1} end, 0, lists:seq(1,40)),
    true = map_key_slice:present(Key, #{Key => true}),
    yes = map_key_slice:guarded(Key, #{Key => true}),
    0 = map_key_slice:size_empty(0),
    2 = map_key_slice:size_flat(0),
    33 = map_key_slice:size_large(0),
    1 = map_key_slice:cardinality(#{Key => true}),
    yes = map_key_slice:guard_size_large(0),
    no = map_key_slice:guard_size(123),
    try map_key_slice:cardinality(123) of _ -> error(expected_badmap)
    catch error:{badmap,123} -> ok end,
    <<"payload">> = map_key_slice:copy(<<"payload">>),
    io:format("OTP_MAP_KEY_REFERENCE_OK present=true missing=true badmap=true guards=true compound_key=true cardinality=true~n").
