%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(node_slice).
-export([query/1, kept/1, peers/1, kept_peers/1, dynamic/1, identity/1, copy/1]).
query(_) -> node().
peers(_) -> nodes().
kept_peers(Input) -> Names = nodes(), identity(Input), Names.
kept(Input) -> Name = node(), identity(Input), Name.
dynamic({Module,Function}) -> apply(Module,Function,[]).
identity(Input) -> Input.
copy(Input) ->
    nonode@nohost = query(Input),
    nonode@nohost = kept(Input),
    nonode@nohost = dynamic({erlang,node}),
    [] = peers(Input),
    [] = kept_peers(Input),
    [] = dynamic({erlang,nodes}),
    Input.
