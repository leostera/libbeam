%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(div_reference).
-export([run/1]).
run(File) ->
    {ok, Bytes} = file:read_file(File),
    Lines = [Line || Line <- binary:split(Bytes, <<"\n">>, [global]), Line =/= <<>>],
    lists:foreach(fun(Line) ->
        [X,Y,Q,R] = [binary_to_integer(N,16) || N <- binary:split(Line, <<" ">>, [global])],
        lists:foreach(fun({SX,SY}) ->
            Dividend = X*SX, Divisor = Y*SY,
            true = (Q*SX*SY =:= div_slice:divide(Dividend, Divisor)),
            true = (R*SX =:= Dividend rem Divisor)
        end, [{1,1},{1,-1},{-1,1},{-1,-1}])
    end, Lines),
    yes = div_slice:guarded(84,2),
    no = div_slice:guarded(84,0),
    no = div_slice:guarded(84,2.0),
    no = div_slice:guarded(84,true),
    lists:foreach(fun(Divisor) ->
        try div_slice:divide(84, Divisor) of _ -> error(expected_badarith)
        catch error:badarith -> ok end
    end, [0,2.0,true]),
    -42 = div_slice:kept(-85,2),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = div_slice:copy(Payload),
    io:format("OTP_DIV_REFERENCE_OK vectors=~p signs=true remainder=true badarith=true~n", [length(Lines)]).
