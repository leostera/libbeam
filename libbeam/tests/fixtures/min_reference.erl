%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(min_reference).
-export([run/0]).
run() ->
    -2 = min_slice:minimum(-2, 3),
    1 = min_slice:minimum(9, 1),
    5 = min_slice:minimum(5, 5),
    false = min_slice:minimum(false, true),
    99 = min_slice:minimum(true, 99),
    true = (1.0 =:= min_slice:float_first()),
    true = (1 =:= min_slice:integer_first()),
    %% Valid in OTP, explicitly outside current native ordering support.
    {1} = min_slice:tuple_refusal(),
    chosen = min_slice:guard_refusal(),
    <<"payload">> = min_slice:copy(<<"payload">>),
    io:format("OTP_MIN_REFERENCE_OK first_operand_tie=true compound_is_valid_in_otp=true~n").
