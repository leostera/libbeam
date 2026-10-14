%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(node_reference).
-export([run/0]).
run() ->
    nonode@nohost = node(),
    nonode@nohost = node_slice:query(ignored),
    nonode@nohost = node_slice:kept(ignored),
    nonode@nohost = node_slice:dynamic({erlang,node}),
    nonode@nohost = node_locations:query(ignored),
    nonode@nohost = node_locations:query_y(ignored),
    [] = nodes(),
    [] = node_slice:peers(ignored),
    [] = node_slice:kept_peers(ignored),
    [] = node_slice:dynamic({erlang,nodes}),
    <<1,2,3>> = node_slice:copy(<<1,2,3>>),
    nonode@nohost = node_slice:container(ignored),
    nonode@nohost = node_slice:dynamic_container({erlang,node,make_ref()}),
    {Pid,Monitor} = spawn_monitor(fun() -> ok end),
    receive {'DOWN',Monitor,process,Pid,normal} -> ok end,
    nonode@nohost = node(Pid),
    lists:foreach(fun(T) ->
        false = node_slice:guard_node(T),
        try node_slice:bad_container(T) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[true,42,1.0,{},[],<<>>,#{}]),
    io:format("OTP_NODE_REFERENCE_OK unnamed=true compiled_x_y=true dynamic=true~n").
