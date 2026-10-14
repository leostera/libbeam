%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_prepare_loading_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:prepare_loading([], []).
identity(Input) -> Input.
