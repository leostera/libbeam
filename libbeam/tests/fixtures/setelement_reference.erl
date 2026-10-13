%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(setelement_reference).
-export([run/0]).
run() ->
    {first, new, last} = setelement_slice:sample(),
    Source = {[7], 0, [7]}, Replacement = {true, [7]},
    {[7], Replacement, [7]} = setelement_slice:replace(2, Source, Replacement),
    {Replacement, 0, [7]} = setelement_slice:replace(1, Source, Replacement),
    {[7], 0, Replacement} = setelement_slice:replace(3, Source, Replacement),
    {[7], 0, [7]} = Source,
    lists:foreach(fun({Index, Tuple}) ->
        try setelement_slice:replace(Index, Tuple, new) of
            _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end, [{0,Source},{-1,Source},{4,Source},{true,Source},{2,[]},{2,{}}]),
    Payload = binary:copy(<<0,1,255>>, 10000),
    Payload = setelement_slice:copy(Payload),
    io:format("OTP_SETELEMENT_REFERENCE_OK copy=true badarg=true binary=true~n").
