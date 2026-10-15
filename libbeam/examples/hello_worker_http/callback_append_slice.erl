%% SPDX-License-Identifier: Apache-2.0
%% Executable witness: an OTP-compiled private append after an indirect callback.
%% Its four X-to-Y moves also exercise the native move_window4 opcode.
-module(callback_append_slice).
-export([run/1, run/2]).

run(Bytes) -> run(Bytes, fun (Byte) -> Byte end).

run(Bytes, Callback) when is_binary(Bytes), is_function(Callback, 1) ->
    Accumulator = <<>>,
    escape(Bytes, Callback, Accumulator).

escape(<<Byte, Rest/binary>>, Callback, Accumulator) ->
    _ = Callback(Byte),
    escape(Rest, Callback, <<Accumulator/binary, Byte>>);
escape(<<>>, _Callback, Accumulator) ->
    Accumulator.
