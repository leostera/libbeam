% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_disconnect_node_1).
-export([entry/1,identity/1]).
entry(_Input) -> erlang:disconnect_node(undefined).
identity(Input) -> Input.
