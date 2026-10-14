%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(monitor_slice).
-export([identity/1, copy/1, named/1, local_name/1, missing/1, dead/1, cancel/1,
         arm/1, drain/1, self_monitor/1, bad_kind/1, bad_target/1,
         bad_name/1, bad_node/1, bad_reference/1]).
identity(B) -> B.
self_monitor(B) ->
    Ref = monitor(process,self()),
    true = demonitor(Ref),
    true = demonitor(Ref),
    B.
bad_kind(B) -> monitor(B,self()), B.
bad_target(B) -> monitor(process,B), B.
bad_name(B) -> monitor(process,{B,node()}), B.
bad_node(B) -> monitor(process,{monitor_target,B}), B.
bad_reference(B) -> demonitor(B), B.
copy(B) ->
    Pid = spawn(fun() -> receive stop -> ok end end),
    Ref = monitor(process,Pid),
    Pid ! stop,
    receive {'DOWN',Ref,process,Pid,normal} -> B end.
arm(B) ->
    Parent = self(),
    spawn(fun() ->
        true = register(monitor_target,self()),
        Parent ! ready,
        receive stop -> ok end
    end),
    receive ready -> B end.
drain(B) -> watch_name(B,{monitor_target,node()}).
named(B) -> B = arm(B), watch_name(B,monitor_target).
local_name(B) -> B = arm(B), drain(B).
watch_name(B,Target) ->
    Ref = monitor(process,Target),
    monitor_target ! stop,
    Node = node(),
    receive {'DOWN',Ref,process,{monitor_target,Node},normal} -> B end.
missing(B) ->
    Ref = monitor(process,monitor_absent),
    Node = node(),
    receive {'DOWN',Ref,process,{monitor_absent,Node},noproc} -> B end.
dead(B) ->
    {Pid,First} = spawn_monitor(fun() -> ok end),
    receive {'DOWN',First,process,Pid,normal} -> ok end,
    Ref = monitor(process,Pid),
    receive {'DOWN',Ref,process,Pid,noproc} -> B end.
cancel(B) ->
    Pid = spawn(fun() -> receive stop -> ok end end),
    Ref = monitor(process,Pid),
    true = demonitor(Ref),
    true = demonitor(Ref),
    Keep = monitor(process,Pid),
    Pid ! stop,
    receive {'DOWN',Keep,process,Pid,normal} -> ok end,
    receive {'DOWN',Ref,_,_,_} -> <<"unexpected cancelled DOWN">> after 0 -> B end.
