%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_call_on_load_function_1).
-export([entry/1, identity/1]).
entry(_) -> erlang:call_on_load_function([]).
identity(Input) -> Input.
