%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_display_1).
-export([entry/1, identity/1]).
entry(_) -> erlang:display([]).
identity(Input) -> Input.
