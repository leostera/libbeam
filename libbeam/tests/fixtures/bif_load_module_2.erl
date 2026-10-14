%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_load_module_2).
-export([present/1, absent/1, bad_type/1, identity/1]).
present(Bytes) ->
    case erlang:load_module(?MODULE, Bytes) of
        {module, ?MODULE} -> <<"already loaded">>;
        _ -> <<"incorrect acknowledgement">>
    end.
absent(Bytes) -> erlang:load_module(bif_unpublished_module, Bytes).
bad_type(_) -> erlang:load_module([], []).
identity(Bytes) -> Bytes.
