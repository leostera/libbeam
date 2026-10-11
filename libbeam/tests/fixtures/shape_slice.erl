%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(shape_slice).
-export([tagged/1, tuple/1, list/1, head/1, build/1, tuple_binary/1,
         list_binary/1, arities/1, two_arities/1, tagged_saved/1, head_saved/1]).
tagged({tag, Binary}) -> Binary;
tagged(_) -> <<"not-tagged">>.
tuple({A, B}) -> {B, A};
tuple(_) -> <<"not-tuple">>.
list([Head | Tail]) -> {Head, Tail};
list(_) -> <<"not-list">>.
head([Head | _]) -> Head;
head(_) -> <<"not-list">>.
build(Binary) -> [Binary, Binary].
tuple_binary(Binary) -> ?MODULE:tagged({tag, Binary}).
list_binary(Binary) -> ?MODULE:head([Binary]).
arities(Term) ->
    case Term of
        {} -> <<"0">>;
        {_} -> <<"1">>;
        {_, _} -> <<"2">>;
        _ -> <<"other">>
    end.
tagged_saved({tag, Binary}) -> erlang:get_module_info(?MODULE), Binary.
head_saved([Head | _]) -> erlang:get_module_info(?MODULE), Head.
two_arities(Term) ->
    case Term of
        {} -> <<"0">>;
        {_, _} -> <<"2">>;
        _ -> <<"other">>
    end.
