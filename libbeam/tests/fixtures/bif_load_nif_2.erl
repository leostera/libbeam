%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_load_nif_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:load_nif("must-not-open-a-library", 0).
identity(Input) -> Input.
