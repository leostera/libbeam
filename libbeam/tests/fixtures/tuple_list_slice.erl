%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(tuple_list_slice).
-export([convert/1, identity/1, copy/1]).
convert(Input) -> erlang:tuple_to_list(Input).
identity(Input) -> Input.
copy(Input) ->
    [Input,42,Input] = convert({Input,42,Input}),
    [] = convert({}),
    Input.
