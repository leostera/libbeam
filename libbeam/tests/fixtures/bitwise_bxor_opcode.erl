% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bitwise_bxor_opcode).
-export([entry/1,identity/1]).
entry(Input) ->
    {_,_,Value} = erlang:binary_to_term(Input),
    case Value bxor 3 of 4 -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
