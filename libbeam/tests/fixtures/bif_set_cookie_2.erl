%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_set_cookie_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:set_cookie([], []).
identity(Input) -> Input.
