%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(make_tuple_slice).
-export([build/1, identity/1, copy/1]).
build({Count,Value}) -> erlang:make_tuple(Count,Value).
identity(Input) -> Input.
copy(Input) ->
    {Input,Input,Input} = build({3,Input}),
    {} = build({0,Input}),
    Input.
