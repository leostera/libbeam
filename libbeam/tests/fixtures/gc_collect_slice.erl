%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(gc_collect_slice).
-export([identity/1,copy/1,invalid/1]).
identity(B) -> B.
copy(B) ->
    Root = {B,[B],fun() -> B end},
    undefined = put(gc_root,Root),
    true = garbage_collect(),
    {B,[B],F} = erase(gc_root),
    B = F(),
    true = garbage_collect(self()),
    Parent = self(),
    {Pid,Ref} = spawn_monitor(fun() ->
        put(child_root,{B,[B]}),
        Parent ! ready,
        receive resume ->
            {B,[B]} = erase(child_root),
            Parent ! {copied,B}
        end
    end),
    receive ready -> ok end,
    true = garbage_collect(Pid),
    Pid ! resume,
    receive {copied,B} -> ok end,
    receive {'DOWN',Ref,process,Pid,normal} -> ok end,
    false = garbage_collect(Pid),
    B.
invalid(B) -> garbage_collect(B).
