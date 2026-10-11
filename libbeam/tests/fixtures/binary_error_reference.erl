%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Stock-OTP-only exception oracle. Never loaded into libbeam.
-module(binary_error_reference).
-export([run/0, utf32/1, integer/2, floating/2, binary/2]).
utf32(Value) -> <<Value/utf32>>.
integer(Value, Size) -> <<Value:Size>>.
floating(Value, Size) -> <<Value:Size/float>>.
binary(Value, Size) -> <<Value:Size/binary>>.
run() ->
    lists:foreach(fun({Function, Args}) ->
        try apply(?MODULE, Function, Args) of
            _ -> error(expected_failure)
        catch error:badarg:Stack ->
            [{_,_,_,Extra}|_] = Stack,
            Info = proplists:get_value(error_info, Extra),
            io:format("~w~n", [lists:sort(maps:to_list(Info))])
        end
    end, [{utf32,[16#110000]}, {utf32,[-1]}, {integer,[1,-1]},
          {floating,[1.0,7]}, {binary,[42,1]}]).
