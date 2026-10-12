%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(memory_slice).
-export([identity/1, grow/1, consume/1]).
identity(Binary) -> Binary.
%% Dynamic integer-segment storage followed by an ordinary binary consumer.
%% This is real native allocation work, not an oversized literal or host buffer.
grow(Input) ->
    Bits = case Input =:= <<8,0,0,0>> of true -> 134217728; false -> 0 end,
    Binary = <<0:Bits>>,
    ?MODULE:consume(Binary).
consume(<<>>) -> <<"empty">>;
consume(_) -> <<"grown">>.
