%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(fun_slice).
-export([echo/1, factory/1, closed/1, closed_factory/1, nested/1, badarity/1, reply/1, process_factory/1, process_nested/1, is_fun/1, is_zero/1, is_arity/2, process_loop/1,
         spawn_finite/1, spawn_loop/1, spawn_badarg/1, spawn_refused/1,
         send_self/1, send_child/1, send_badarg/1, receive_roundtrip/1, receive_timeout/1, receive_self/1, named_self/1, named_duplicate/1, missing_name/1, guest_error/1, guest_error_info/1, marked_request/1]).
is_fun(F) when is_function(F) -> true;
is_fun(_) -> false.
is_zero(F) when is_function(F,0) -> true;
is_zero(_) -> false.
is_arity(F,N) when is_function(F,N) -> true;
is_arity(_,_) -> false.
marked_request(B) ->
    self() ! noise,
    Ref = make_ref(),
    self() ! {Ref,B},
    receive {Ref,Value} -> Value end.
named_self(B) ->
    true = register(probe_state,self()),
    probe_state ! {reply,B},
    receive {reply,R} -> R end.
named_duplicate(B) ->
    true = register(probe_state,self()),
    register(probe_state,self()), B.
missing_name(B) -> absent_state ! B, B.
guest_error(B) -> error({requested,B}).
guest_error_info(B) ->
    erlang:error({requested,B},[B],[{error_info,#{module => erl_erts_errors}}]).
receive_self(B) ->
    self() ! {reply,fun() -> B end},
    receive {reply,F} -> F() end.
receive_roundtrip(B) ->
    P = erlang:spawn(erlang, apply, [fun() -> actor_loop(0) end, []]),
    self() ! {noise,B},
    1 = actor_request(P),
    2 = actor_request(P),
    receive {noise,B} -> integer_to_binary(2) end.
actor_request(P) ->
    Ref = {actor_ref(),P},
    P ! {self(),Ref},
    receive {Ref,Count} -> Count after 5000 -> timeout end.
actor_ref() -> make_ref().
actor_loop(Count) ->
    receive
        {From,Ref} ->
            Next = Count + 1,
            From ! {Ref,Next},
            actor_loop(Next)
    end.
receive_timeout(B) ->
    receive {reply,Reply} -> Reply after 2 -> B end.
send_self(B) -> self() ! {B,factory(B)}, B.
send_child(B) ->
    P = erlang:spawn(erlang, apply, [fun() -> spinning(B) end, []]),
    P ! {self(),make_ref(),B}, B.
send_badarg(B) -> B ! B, B.
spawn_finite(B) -> erlang:spawn(erlang, apply, [fun() -> {B,self()} end, []]), B.
spawn_loop(B) -> erlang:spawn(erlang, apply, [fun() -> spinning(B) end, []]), B.
spawn_badarg(B) -> erlang:spawn(B, apply, []), B.
spawn_refused(B) -> erlang:spawn(erlang, apply, [B, []]), B.
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
