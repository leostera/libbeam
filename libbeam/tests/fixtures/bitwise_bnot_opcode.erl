% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bitwise_bnot_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    true = run(6) =:= -7,
    Big = 1 bsl 70,
    true = run(Big) =:= -1180591620717411303425,
    true = run(-Big) =:= Big - 1,
    <<"ok">>.
run(Value) -> bnot Value.
identity(Input) -> Input.
