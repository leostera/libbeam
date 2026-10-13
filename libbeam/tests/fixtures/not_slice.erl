%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(not_slice).
-export([negate/1, identity/1, copy/1]).
negate(Value) -> not Value.
identity(Input) -> Input.
copy(Input) ->
    case {negate(true), negate(false)} of
        {false, true} -> Input
    end.
