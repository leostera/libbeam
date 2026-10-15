%% SPDX-License-Identifier: Apache-2.0
%% Guard-proven dynamic float load (fload_Sl) and subtraction.
-module(float_load_slice).
-export([run/1]).

run(Bytes) ->
    Value = binary_to_float(Bytes),
    case Value of
        Float when is_float(Float) -> <<(Float - 2.5):64/float>>
    end.
