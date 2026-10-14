#!/usr/bin/env escript
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-mode(compile).
main([Beam]) ->
    {ok, Bytes} = file:read_file(Beam),
    {module, process_contracts} = code:load_binary(process_contracts, Beam, Bytes),
    lists:foreach(fun witness/1,
        [selective_fifo, names, trapped_link, unlink_before_exit, monitor_tag,
         demonitor_flush, alias_explicit, alias_reply, timer_delivery,
         timer_cancel, timer_cancel_race, absolute_timer]),
    io:format("PROCESS_REFERENCE_OK cases=12~n").
witness(Function) ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        Result = apply(process_contracts, Function, [<<0,1,255,127,0,42>>]),
        Parent ! {self(), Result}
    end),
    receive
        {Pid, <<"ok">>} -> ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({Function, Reason})
    after 10000 -> error({Function, timeout}) end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok
    after 10000 -> error({Function, did_not_exit}) end.
