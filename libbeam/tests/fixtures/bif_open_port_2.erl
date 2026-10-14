%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_open_port_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:open_port([], []).
identity(Input) -> Input.
