%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(error2_reference).
-export([run/0]).
run() ->
    Reason = binary:copy(<<109>>,100),
    lists:foreach(fun(Args) ->
        Expected = case Args of [] -> []; [_|_] -> Args; _ -> 1 end,
        try error2_slice:crash({Reason,Args}) of _ -> erlang:error(missing_error)
        catch error:Reason:Trace -> [{error2_slice,crash,Expected,_}|_] = Trace end
    end,[[],[Reason],[Reason|ok],ok,42,{Reason}]),
    Reason = error2_slice:copy(Reason),
    io:format("OTP_ERROR2_REFERENCE_OK reason=true arbitrary_args=true caller_frame=true improper_args=true monitor=true~n").
