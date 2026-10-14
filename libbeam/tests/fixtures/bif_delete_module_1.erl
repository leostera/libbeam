%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_delete_module_1).
-export([entry/1, identity/1]).
entry(_) -> erlang:delete_module([]).
identity(Input) -> Input.
