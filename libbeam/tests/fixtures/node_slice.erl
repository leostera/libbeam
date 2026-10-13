%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(node_slice).
-export([query/1, kept/1, dynamic/1, identity/1, copy/1]).
query(_) -> node().
kept(Input) -> Name = node(), identity(Input), Name.
dynamic({Module,Function}) -> apply(Module,Function,[]).
identity(Input) -> Input.
copy(Input) ->
    nonode@nohost = query(Input),
    nonode@nohost = kept(Input),
    nonode@nohost = dynamic({erlang,node}),
    Input.
