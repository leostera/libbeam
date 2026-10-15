%% SPDX-License-Identifier: Apache-2.0
%% An OTP private append survives a remote call that does not receive the accumulator.
-module(remote_append_slice).
-export([run/1, echo/1]).

run(Bytes) when is_binary(Bytes) -> append(Bytes, <<>>).
echo(Byte) -> Byte.

append(<<Byte, Rest/binary>>, Accumulator) ->
    _ = ?MODULE:echo(Byte),
    append(Rest, <<Accumulator/binary, Byte>>);
append(<<>>, Accumulator) -> Accumulator.
