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
    io:format("OTP_NODE_REFERENCE_OK unnamed=true compiled_x_y=true dynamic=true~n").
