%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(times_reference).
-export([run/0]).
run() ->
    42 = times_slice:small(),
    340282366920938463426481119284349108225 = times_slice:wide(),
    -340282366920938463426481119284349108225 = times_slice:negative(),
    8543948143683640329580086824678208458423372292897384441315157445401452359027840161765912880349183
        = times_slice:large(),
    6.0 = times_slice:floating(),
    yes = times_slice:guard_yes(), no = times_slice:guard_bad(),
    42 = times_slice:kept(6, 7),
    lists:foreach(fun(F) ->
        try F() of _ -> error(expected_badarith)
        catch error:badarith -> ok end
    end, [fun times_slice:float_overflow/0, fun times_slice:huge_float/0, fun times_slice:bad_number/0]),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = times_slice:copy(Payload),
    io:format("OTP_TIMES_REFERENCE_OK integer=true float=true badarith=true guard=true~n").
