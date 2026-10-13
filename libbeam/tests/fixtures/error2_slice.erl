%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(error2_slice).
-export([crash/1, identity/1, copy/1]).
crash({Reason,Args}) -> erlang:error(Reason,Args).
identity(Input) -> Input.
copy(Input) ->
    {Pid,Ref} = spawn_monitor(fun() -> crash({Input,[Input]}) end),
    receive
        {'DOWN',Ref,process,Pid,{Input,[{?MODULE,crash,[Input],_}|_]}} -> Input
    end.
