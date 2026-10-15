%% SPDX-License-Identifier: Apache-2.0
%% OTP oracle for checked_list_slice.S. The assembly differs only by
%% removing OTP's is_nonempty_list guard, so nil tests the native yxx guard.
-module(checked_list_slice).
-export([run/1]).
run(Bytes) when is_binary(Bytes) ->
    List = binary_to_list(Bytes),
    Prefix = integer_to_binary(byte_size(Bytes)),
    case List of
        [Head | Tail] -> <<Head, Prefix/binary, (length(Tail))>>
    end.
