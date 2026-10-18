% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bitwise_bxor_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    case run(<<6,3>>) of <<5>> -> <<"ok">>; _ -> <<"contract mismatch">> end.
run(<<Left:8,Right:8>>) -> <<(Left bxor Right):8>>.
identity(Input) -> Input.
