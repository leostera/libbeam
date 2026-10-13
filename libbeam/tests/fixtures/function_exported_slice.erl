%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(function_exported_slice).
-export([query/1, identity/1, copy/1]).
query({Module,Function,Arity}) -> erlang:function_exported(Module,Function,Arity).
identity(Input) -> private(Input).
private(Input) -> Input.
copy(Input) ->
    case {query({?MODULE,identity,1}),query({?MODULE,private,1}),
          query({erlang,apply,3}),query({unloaded_probe_module,missing,1})} of
        {true,false,true,false} -> Input
    end.
