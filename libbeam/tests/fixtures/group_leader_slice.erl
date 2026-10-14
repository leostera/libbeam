%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(group_leader_slice).
-export([identity/1,copy/1,lifecycle/1,invalid/1,invalid_target/1,dead_target/1]).
identity(B) -> B.
%% Keep the exact-credit witness single-process: receive waiting costs in the
%% lifecycle contract legitimately depend on which actor reaches a wait first.
copy(B) ->
    Original = group_leader(),
    Self = self(),
    true = group_leader(Self,Self),
    Self = group_leader(),
    true = group_leader(Original,Self),
    B.
lifecycle(B) ->
    Original = group_leader(),
    Parent = self(),
    {Pid,Ref} = spawn_monitor(fun() ->
        Original = group_leader(),
        Parent ! ready,
        receive query ->
            true = garbage_collect(),
            Leader = group_leader(),
            Parent ! {leader,Leader},
            {Child,ChildRef} = spawn_monitor(fun() -> Leader = group_leader(), Parent ! inherited end),
            receive {'DOWN',ChildRef,process,Child,normal} -> ok end
        end
    end),
    receive ready -> ok end,
    true = group_leader(Pid,Pid),
    Pid ! query,
    receive {leader,Pid} -> ok end,
    receive inherited -> ok end,
    receive {'DOWN',Ref,process,Pid,normal} -> ok end,
    true = group_leader(Pid,self()),
    Pid = group_leader(),
    {Last,LastRef} = spawn_monitor(fun() -> Pid = group_leader(), Parent ! dead_leader_inherited end),
    receive dead_leader_inherited -> ok end,
    receive {'DOWN',LastRef,process,Last,normal} -> ok end,
    true = group_leader(Original,self()),
    B.
invalid(B) -> group_leader(B,self()).
invalid_target(B) -> group_leader(self(),B).
dead_target(_) ->
    {Pid,Ref} = spawn_monitor(fun() -> ok end),
    receive {'DOWN',Ref,process,Pid,normal} -> group_leader(self(),Pid) end.
