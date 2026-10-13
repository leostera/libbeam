%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(rem_reference).
-export([run/1]).
run(File) ->
    {ok, Bytes} = file:read_file(File),
    Lines = [Line || Line <- binary:split(Bytes, <<"\n">>, [global]), Line =/= <<>>],
    lists:foreach(fun(Line) ->
        [X,Y,Q,R] = [binary_to_integer(N,16) || N <- binary:split(Line, <<" ">>, [global])],
        lists:foreach(fun({SX,SY}) ->
            Dividend = X*SX, Divisor = Y*SY,
            true = (R*SX =:= rem_slice:remainder(Dividend, Divisor)),
            true = (Q*SX*SY =:= Dividend div Divisor)
        end, [{1,1},{1,-1},{-1,1},{-1,-1}])
    end, Lines),
    yes = rem_slice:guarded(85,43),
    no = rem_slice:guarded(84,0),
    no = rem_slice:guarded(84,2.0),
    no = rem_slice:guarded(84,true),
    lists:foreach(fun(Divisor) ->
        try rem_slice:remainder(84, Divisor) of _ -> error(expected_badarith)
        catch error:badarith -> ok end
    end, [0,2.0,true]),
    -1 = rem_slice:kept(-85,2),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = rem_slice:copy(Payload),
    io:format("OTP_REM_REFERENCE_OK vectors=~p dividend_sign=true badarith=true~n", [length(Lines)]).
