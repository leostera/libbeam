#!/usr/bin/env escript
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Independent language oracle; never serves libbeam requests.
-mode(compile).
main([Beam]) ->
    {ok, Bytes} = file:read_file(Beam),
    {module, gc_roots} = code:load_binary(gc_roots, Beam, Bytes),
    Payload = binary:copy(list_to_binary(lists:seq(0,255)),32),
    lists:foreach(fun(Function) -> witness(Function, Payload) end,
        [registers_stack, dictionary, mailbox, closures, binary_views,
         pending_exception, timer_payload, alias_reference, hibernate_zero, hibernate_mfa]),
    io:format("GC_REFERENCE_OK roots=10~n").
witness(Function, Payload) ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() -> Parent ! {self(), apply(gc_roots, Function, [Payload])} end),
    ready(Pid, 5000),
    case Function of
        hibernate_mfa ->
            %% hibernate/3 discards the wrapper's continuation. Observe the
            %% actual wake callback return instead of expecting apply/3 back.
            1 = erlang:trace_pattern({gc_roots,wake,1}, [{'_',[],[{return_trace}]}], [local]),
            1 = erlang:trace(Pid, true, [call]);
        _ -> ok
    end,
    %% Ordered messages allow both ordinary and hibernating continuations to
    %% finish. Collection counters are asserted separately by the native test.
    Pid ! collect,
    Pid ! resume,
    receive
        {Pid, Payload} -> ok;
        {trace, Pid, return_from, {gc_roots,wake,1}, Payload} when Function =:= hibernate_mfa -> ok;
        {'DOWN', Ref, process, Pid, Reason} when Function =/= hibernate_mfa -> error({Function, Reason})
    after 5000 -> error({Function, timeout}) end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok after 5000 -> error({Function, did_not_exit}) end.
ready(_, 0) -> error(root_not_ready);
ready(Pid, Attempts) ->
    case {whereis(gc_probe), process_info(Pid, dictionary)} of
        {Pid, {dictionary, Values}} ->
            case lists:keyfind(phase, 1, Values) of
                {phase, before_gc} -> ok;
                _ -> receive after 1 -> ready(Pid, Attempts - 1) end
            end;
        _ -> receive after 1 -> ready(Pid, Attempts - 1) end
    end.
