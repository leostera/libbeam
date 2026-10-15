% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_trace_pattern_3).
-export([entry/1,identity/1]).
entry(_Input) ->
    _ = erlang:trace_pattern({?MODULE,identity,1},true,[local]),
    <<"unexpected success">>.
identity(Input) -> Input.
