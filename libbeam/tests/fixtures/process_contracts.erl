%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(process_contracts).
-export([identity/1, selective_fifo/1, names/1, trapped_link/1, unlink_before_exit/1,
         monitor_tag/1, demonitor_flush/1, alias_explicit/1, alias_reply/1,
         timer_delivery/1, timer_cancel/1, timer_cancel_race/1, absolute_timer/1]).
identity(Input) -> Input.
selective_fifo(Input) ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        Parent ! {item, 1, Input}, Parent ! {item, 2, Input}, Parent ! {item, 3, Input}
    end),
    receive {item, 3, Input} -> ok end,
    receive {item, First, Input} -> 1 = First end,
    receive {item, Second, Input} -> 2 = Second end,
    receive {'DOWN', Ref, process, Pid, normal} -> <<"ok">> end.
names(Input) ->
    {Pid, Ref} = spawn_monitor(fun named_loop/0),
    true = register(contract_actor, Pid),
    Pid = whereis(contract_actor),
    true = contains(contract_actor, registered()),
    contract_actor ! {ping, self(), Input},
    receive {reply, Pid, Input} -> ok end,
    true = unregister(contract_actor),
    undefined = whereis(contract_actor),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> <<"ok">> end.
named_loop() ->
    receive
        {ping, From, Input} -> From ! {reply, self(), Input}, named_loop();
        stop -> ok
    end.
contains(X, [X|_]) -> true;
contains(X, [_|Rest]) -> contains(X, Rest);
contains(_, []) -> false.
trapped_link(_) ->
    false = process_flag(trap_exit, true),
    Pid = spawn_link(fun() -> exit(link_reason) end),
    receive {'EXIT', Pid, link_reason} -> ok end,
    true = process_flag(trap_exit, false),
    <<"ok">>.
unlink_before_exit(_) ->
    Pid = spawn_link(fun() -> receive die -> exit(unlinked_reason) end end),
    Ref = monitor(process, Pid),
    true = unlink(Pid),
    Pid ! die,
    receive {'DOWN', Ref, process, Pid, unlinked_reason} -> ok end,
    receive {'EXIT', Pid, _} -> error(unlinked_exit) after 0 -> <<"ok">> end.
monitor_tag(_) ->
    Pid = spawn(fun() -> receive stop -> ok end end),
    Ref = monitor(process, Pid, [{tag, custom_down}]),
    Pid ! stop,
    receive {custom_down, Ref, process, Pid, normal} -> <<"ok">> end.
demonitor_flush(_) ->
    {Pid, First} = spawn_monitor(fun() -> receive stop -> ok end end),
    Second = monitor(process, Pid),
    true = demonitor(First, [flush, info]),
    Pid ! stop,
    receive {'DOWN', Second, process, Pid, normal} -> ok end,
    receive {'DOWN', First, process, Pid, _} -> error(cancelled_monitor_delivered)
    after 0 -> <<"ok">> end.
alias_explicit(Input) ->
    Alias = erlang:alias(),
    Alias ! {first, Input},
    receive {first, Input} -> ok end,
    true = erlang:unalias(Alias),
    Alias ! {dropped, Input},
    false = erlang:unalias(Alias),
    receive {dropped, _} -> error(inactive_alias_delivered) after 0 -> <<"ok">> end.
alias_reply(Input) ->
    Alias = erlang:alias([reply]),
    Alias ! {first, Input},
    receive {first, Input} -> ok end,
    Alias ! {dropped, Input},
    false = erlang:unalias(Alias),
    receive {dropped, _} -> error(reply_alias_reused) after 0 -> <<"ok">> end.
timer_delivery(Input) ->
    Ref = erlang:start_timer(5, self(), Input),
    receive {timeout, Ref, Input} -> ok end,
    false = erlang:read_timer(Ref),
    false = erlang:cancel_timer(Ref),
    SendRef = erlang:send_after(0, self(), {plain_timer, Input}),
    true = is_reference(SendRef),
    receive {plain_timer, Input} -> <<"ok">> end.
timer_cancel(Input) ->
    Ref = erlang:send_after(60000, self(), {cancelled, Input}),
    Remaining = erlang:read_timer(Ref),
    true = is_integer(Remaining), true = Remaining >= 0, true = Remaining =< 60000,
    Cancelled = erlang:cancel_timer(Ref),
    true = is_integer(Cancelled), true = Cancelled >= 0, true = Cancelled =< Remaining,
    false = erlang:read_timer(Ref), false = erlang:cancel_timer(Ref),
    receive {cancelled, _} -> error(cancelled_timer_delivered) after 0 -> <<"ok">> end.
timer_cancel_race(Input) -> timer_races(32, Input), <<"ok">>.
timer_races(0, _) -> ok;
timer_races(N, Input) ->
    Ref = erlang:start_timer(0, self(), Input),
    case erlang:cancel_timer(Ref) of
        false -> receive {timeout, Ref, Input} -> ok after 5000 -> error(lost_expiry) end;
        0 -> ok
    end,
    receive {timeout, Ref, _} -> error(duplicate_or_cancelled_expiry) after 0 -> ok end,
    timer_races(N - 1, Input).
absolute_timer(Input) ->
    When = erlang:monotonic_time(millisecond) + 5,
    Ref = erlang:start_timer(When, self(), Input, [{abs, true}]),
    receive {timeout, Ref, Input} -> <<"ok">> end.
