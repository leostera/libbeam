%% SPDX-License-Identifier: Apache-2.0
%% OTP-compiled dynamic float load and subtraction, returning portable binary bits.
-module(float_sub_slice).
-export([run/1]).

run(Bytes) when is_binary(Bytes) ->
    Value = binary_to_float(Bytes),
    Difference = Value - 2.5,
    <<Difference:64/float>>.
