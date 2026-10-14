%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_finish_after_on_load_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:finish_after_on_load([], []).
identity(Input) -> Input.
