#!/usr/bin/env escript
%%! +S 1:1 +SDcpu 1:1 +SDio 1 +A 0
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
main([Output]) ->
    Terms = [-1, -(1 bsl 64), -0.5, true, false, ok, [], [ok|false], {},
             {ok,true,[]}, #{}, <<165>>, <<82:7>>, self(), make_ref(), list_to_port("#Port<0.0>")],
    Functions = [is_bitstring,is_boolean,is_float,is_map,is_number,is_pid,
                 is_port,is_reference,is_tuple],
    Rows = [begin
        Mask = lists:foldl(fun({F,Bit},Acc) ->
            case apply(erlang,F,[Term]) of true -> Acc bor (1 bsl Bit); false -> Acc end
        end,0,lists:zip(Functions,lists:seq(0,8))),
        Size = try tuple_size(Term) catch error:badarg -> -1 end,
        io_lib:format("~B ~B ~B~n",[Index,Mask,Size])
    end || {Index,Term} <- lists:zip(lists:seq(0,length(Terms)-1),Terms)],
    ok = file:write_file(Output,Rows),
    io:format("TYPE_REFERENCE_OK otp=~s erts=~s rows=~B~n",
              [erlang:system_info(otp_release),erlang:system_info(version),length(Terms)]).
