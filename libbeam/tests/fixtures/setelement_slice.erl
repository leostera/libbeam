%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(setelement_slice).
-export([replace/3, sample/0, copy/1, identity/1, update_twice/3]).
replace(Index, Tuple, Value) -> erlang:setelement(Index, Tuple, Value).
sample() -> replace(2, {first, old, last}, new).
copy(Input) ->
    {first, Result, last} = replace(2, {first, old, last}, Input),
    Result.
identity(Input) -> Input.
update_twice(Tuple, First, Second) -> setelement(2, setelement(1, Tuple, First), Second).
