%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(node_slice).
-export([query/1, kept/1, peers/1, kept_peers/1, dynamic/1, identity/1, copy/1,
         container/1,guard_node/1,dynamic_container/1,bad_container/1]).
query(_) -> node().
peers(_) -> nodes().
kept_peers(Input) -> Names = nodes(), identity(Input), Names.
kept(Input) -> Name = node(), identity(Input), Name.
dynamic({Module,Function}) -> apply(Module,Function,[]).
identity(Input) -> Input.
container(_) ->
    Name = node(self()),
    Name = node(make_ref()),
    Name.
guard_node(Term) when node(Term) =:= nonode@nohost -> true;
guard_node(_) -> false.
dynamic_container({Module,Function,Term}) -> apply(Module,Function,[Term]).
bad_container(B) -> node(B).
copy(Input) ->
    nonode@nohost = container(Input),
    nonode@nohost = dynamic_container({erlang,node,self()}),
    Ref = make_ref(),
    true = guard_node(Ref),
    true = erlang:garbage_collect(),
    nonode@nohost = node(Ref),
    true = guard_node(self()),
    false = guard_node(Input),
    false = guard_node({invalid,a,node}),
    false = guard_node(true),
    false = guard_node(42),
    false = guard_node(1.0),
    false = guard_node([]),
    false = guard_node(#{}),
    nonode@nohost = query(Input),
    nonode@nohost = kept(Input),
    nonode@nohost = dynamic({erlang,node}),
    [] = peers(Input),
    [] = kept_peers(Input),
    [] = dynamic({erlang,nodes}),
    Input.
