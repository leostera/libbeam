% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(wait_error_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    try receive after -1 -> ignored end
    catch error:timeout_value -> <<"ok">> end.
identity(Input) -> Input.
