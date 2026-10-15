% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_trace_3).
-export([entry/1,identity/1]).
entry(_Input) ->
    _ = erlang:trace(self(),true,[send]),
    <<"unexpected success">>.
identity(Input) -> Input.
