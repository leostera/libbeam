% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(mixed_div_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    case {run(7,2),run(1,2),run(-7,2)} of
        {3.5,0.5,-3.5} -> <<"ok">>;
        _ -> <<"contract mismatch">>
    end.
run(Left,Right) -> Left / Right.
identity(Input) -> Input.
