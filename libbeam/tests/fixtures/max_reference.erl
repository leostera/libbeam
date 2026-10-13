%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(max_reference).
-export([run/0]).
run() ->
    3 = max_slice:maximum(-2, 3),
    9 = max_slice:maximum(9, 1),
    5 = max_slice:maximum(5, 5),
    true = max_slice:maximum(false, true),
    true = max_slice:maximum(true, 99),
    true = (1.0 =:= max_slice:float_first()),
    true = (1 =:= max_slice:integer_first()),
    %% Valid in OTP, explicitly outside the current runtime ordering profile.
    {2} = max_slice:tuple_refusal(),
    other = max_slice:guard_refusal(),
    io:format("OTP_MAX_REFERENCE_OK first_operand_tie=true compound_is_valid_in_otp=true~n").
