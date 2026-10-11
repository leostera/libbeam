%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(fun_slice).
-export([echo/1, factory/1, closed/1, closed_factory/1, nested/1, badarity/1, reply/1, process_factory/1, process_nested/1, is_fun/1, is_zero/1, is_arity/2, process_loop/1]).
is_fun(F) when is_function(F) -> true;
is_fun(_) -> false.
is_zero(F) when is_function(F,0) -> true;
is_zero(_) -> false.
is_arity(F,N) when is_function(F,N) -> true;
is_arity(_,_) -> false.
process_loop(B) -> fun() -> spinning(B) end.
spinning(B) -> spinning(B).
process_factory(B) ->
    Parent = self(),
    fun() -> {Parent,self(),B} end.
process_nested(B) ->
    Parent = self(),
    F = factory(B),
    fun() -> {Parent,self(),F(42)} end.
reply({42,B}) -> B.
factory(B) -> fun(A) -> {A,B} end.
echo(B) ->
    F = factory(B),
    reply(F(42)).
closed_factory(_) -> fun() -> <<"closed">> end.
closed(B) ->
    F = closed_factory(B),
    F().
nested(B) ->
    F = factory(B),
    G = fun(A) -> F(A) end,
    reply(G(42)).
badarity(B) ->
    F = factory(B),
    F().
