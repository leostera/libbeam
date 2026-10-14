%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_finish_loading_1).
-export([entry/1, identity/1]).
entry(_) -> erlang:finish_loading([]).
identity(Input) -> Input.
