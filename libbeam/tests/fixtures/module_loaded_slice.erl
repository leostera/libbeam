%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(module_loaded_slice).
-export([query/1, identity/1, copy/1]).
query(Module) -> erlang:module_loaded(Module).
identity(Input) -> Input.
copy(Input) ->
    case {query(?MODULE),query(erlang),query(unloaded_module_probe)} of
        {true,true,false} -> Input
    end.
