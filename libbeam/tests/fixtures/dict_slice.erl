%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(dict_slice).
-export([identity/1, copy/1]).
identity(Input) -> Input.
copy(Input) ->
    undefined = get(dict_key),
    undefined = put(dict_key, Input),
    Input = get(dict_key),
    Parent = self(),
    spawn(fun() -> Parent ! {child, get(dict_key)} end),
    receive {child, undefined} -> ok end,
    Input = put(dict_key, changed),
    changed = erase(dict_key),
    undefined = erase(dict_key),
    Input.
