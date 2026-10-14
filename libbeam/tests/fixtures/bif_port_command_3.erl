%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_port_command_3).
-export([entry/1, identity/1]).
entry(_) -> erlang:port_command([], [], []).
identity(Input) -> Input.
