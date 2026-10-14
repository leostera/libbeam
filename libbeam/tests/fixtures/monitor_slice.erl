%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(monitor_slice).
-export([identity/1, copy/1, named/1, local_name/1, missing/1, dead/1, cancel/1,
         arm/1, drain/1, self_monitor/1, bad_kind/1, bad_target/1,
         bad_name/1, bad_node/1, bad_reference/1,
         mfa/1, mfa_peer/1, mfa_child/3, mfa_zero/0, exit_payload/1,
         links/1, unlink_live/1, link_chain/1, link_outcomes/1, linked_arm/1, linked_drain/1, dead_link/1, bad_link/1, bad_unlink/1, bad_module/1, bad_function/1, bad_args/1]).
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
mfa(B) ->
    Leader = group_leader(),
    Token = make_ref(),
    {Pid,Ref} = spawn_monitor(?MODULE,mfa_child,[self(),{B,Token,fun() -> B end,fun erlang:abs/1},Leader]),
    receive {Pid,Token,B} -> ok end,
    receive {'DOWN',Ref,process,Pid,normal} -> ok end,
    {Empty,EmptyRef} = spawn_monitor(?MODULE,mfa_zero,[]),
    receive {'DOWN',EmptyRef,process,Empty,normal} -> B end.
links(B) ->
    false = process_flag(trap_exit,true), Parent = self(),
    true = link(Parent), {links,[]} = process_info(Parent,links),
    Pid = spawn(fun() ->
        receive inspect -> ok end,
        true = link(Parent), {links,[Parent]} = process_info(self(),links),
        Parent ! inspected,
        receive {finish,Reason} -> exit(Reason) end
    end),
    true = link(Pid), true = link(Pid),
    Snapshot = process_info(self(),links), {links,[Pid]} = Snapshot,
    [{links,[Pid]},{trap_exit,true},{links,[Pid]}] = process_info(self(),[links,trap_exit,links]),
    true = erlang:garbage_collect(),
    Pid ! inspect, receive inspected -> ok end,
    Ref = monitor(process,Pid), Reason = {linked,B,make_ref()}, Pid ! {finish,Reason},
    receive {'EXIT',Pid,Reason} -> ok end,
    receive {'DOWN',Ref,process,Pid,Reason} -> ok end,
    {links,[]} = process_info(self(),links), {links,[Pid]} = Snapshot,
    true = link(Pid), receive {'EXIT',Pid,noproc} -> ok end,
    true = unlink(Pid), true = process_flag(trap_exit,false), B.
unlink_live(B) ->
    false = process_flag(trap_exit,true),
    Pid = spawn_link(fun() -> receive stop -> exit({unlinked,B}) end end),
    Ref = monitor(process,Pid), true = unlink(Pid), true = unlink(Pid),
    {links,[]} = process_info(self(),links), {links,[]} = process_info(Pid,links),
    Pid ! stop, receive {'DOWN',Ref,process,Pid,{unlinked,B}} -> ok end,
    true = process_flag(trap_exit,false),
    receive {'EXIT',Pid,_} -> <<"unexpected unlinked EXIT">> after 0 -> B end.
link_chain(B) ->
    Parent = self(),
    {Top,TopRef} = spawn_monitor(fun() ->
        Ancestor = self(),
        Middle = spawn_link(fun() ->
            Leaf = spawn_link(fun() ->
                true = link(Ancestor),
                Parent ! {leaf,self()},
                receive go -> exit({cascade,B}) end
            end),
            Parent ! {middle,self(),Leaf},
            receive never -> ok end
        end),
        Parent ! {top,self(),Middle},
        receive never -> ok end
    end),
    receive {top,Top,Middle} -> ok end,
    receive {middle,Middle,Leaf} -> ok end,
    receive {leaf,Leaf} -> ok end,
    MidRef = monitor(process,Middle), LeafRef = monitor(process,Leaf), Leaf ! go,
    receive {'DOWN',TopRef,process,Top,{cascade,B}} -> ok end,
    receive {'DOWN',MidRef,process,Middle,{cascade,B}} -> ok end,
    receive {'DOWN',LeafRef,process,Leaf,{cascade,B}} -> B end.
link_outcomes(B) ->
    {Normal,NRef} = spawn_monitor(fun() -> receive stop -> ok end end),
    true = link(Normal), Normal ! stop,
    receive {'DOWN',NRef,process,Normal,normal} -> ok end,
    false = process_flag(trap_exit,true), Parent = self(),
    Killed = spawn_link(fun() ->
        false = process_flag(trap_exit,true), Parent ! armed,
        receive never -> ok end
    end),
    receive armed -> ok end,
    KRef = monitor(process,Killed), true = exit(Killed,kill),
    receive {'EXIT',Killed,killed} -> ok end,
    receive {'DOWN',KRef,process,Killed,killed} -> ok end,
    {Failed,ERef} = spawn_monitor(fun() -> receive stop -> error({linked_error,B}) end end),
    true = link(Failed), Failed ! stop,
    receive {'EXIT',Failed,{{linked_error,B},Trace}} when is_list(Trace) -> ok end,
    true = Trace =/= [],
    receive {'DOWN',ERef,process,Failed,{{linked_error,B},Trace}} -> ok end,
    {links,[]} = process_info(self(),links), true = process_flag(trap_exit,false), B.
linked_arm(B) ->
    Parent = self(),
    spawn(fun() ->
        true = register(link_target,self()), Parent ! linked_ready,
        receive {probe,From} -> From ! {saved,B} end,
        receive stop -> exit({linked_saved,B}) end
    end),
    receive linked_ready -> B end.
linked_drain(B) ->
    false = process_flag(trap_exit,true), Pid = whereis(link_target),
    true = link(Pid), Ref = monitor(process,Pid), Pid ! {probe,self()},
    receive {saved,B} -> ok end,
    Pid ! stop, receive {'EXIT',Pid,{linked_saved,B}} -> ok end,
    receive {'DOWN',Ref,process,Pid,{linked_saved,B}} -> ok end,
    {links,[]} = process_info(self(),links),
    true = process_flag(trap_exit,false), B.
dead_link(_) ->
    {Pid,Ref} = spawn_monitor(fun() -> ok end),
    receive {'DOWN',Ref,process,Pid,normal} -> link(Pid) end.
bad_link(B) -> link(B).
bad_unlink(B) -> unlink(B).
exit_payload(B) ->
    Reason = {payload,repeat(20,{B,fun() -> B end},[]),make_ref()},
    {Pid,Ref} = spawn_monitor(fun() -> receive never -> ok end end),
    true = exit(Pid,Reason),
    receive {'DOWN',Ref,process,Pid,Reason} -> B end.
repeat(0,_,Acc) -> Acc;
repeat(N,Value,Acc) -> repeat(N-1,Value,[Value|Acc]).
mfa_peer(B) ->
    {Pid,Ref} = spawn_monitor(group_leader_slice,identity,[B]),
    receive {'DOWN',Ref,process,Pid,normal} -> B end.
mfa_zero() -> ok.
mfa_child(Parent,{B,Token,Closure,External},Leader) ->
    Leader = group_leader(),
    false = process_flag(trap_exit,true),
    true = erlang:garbage_collect(),
    B = Closure(),
    1 = External(-1),
    Parent ! {self(),Token,B}.
bad_module(B) -> spawn_monitor(B,mfa_child,[]).
bad_function(B) -> spawn_monitor(?MODULE,B,[]).
bad_args(B) -> spawn_monitor(?MODULE,mfa_child,[self()|B]).
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
