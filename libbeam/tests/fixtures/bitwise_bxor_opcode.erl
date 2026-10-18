% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bitwise_bxor_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    case run(<<6,3>>) of
        <<5>> -> check_big_and_error();
        _ -> <<"contract mismatch">>
    end.
run(<<Left:8,Right:8>>) -> <<(Left bxor Right):8>>.
check_big_and_error() ->
    Left = 1 bsl 70,
    Right = 1 bsl 68,
    true = (Left bxor Right) =:= (Left + Right),
    true = ((-Left) bxor 3) =:= (-Left + 3),
    true = ((-1) bxor 3) =:= -4,
    <<"ok">>.
identity(Input) -> Input.
