%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(unique_slice).
-export([identity/1,copy/1,next/1,invoke/2,bad_type/1,bad_option/1,bad_tail/1,family/1]).
identity(B) -> B.
invoke(F,Args) -> apply(erlang,F,Args).
pair(Flags) ->
    A=erlang:unique_integer(Flags), B=invoke(unique_integer,[Flags]),
    true=is_integer(A), true=is_integer(B), false=A=:=B, ok.
copy(B) ->
    A=erlang:unique_integer(), C=invoke(unique_integer,[]), false=A=:=C,
    ok=pair([]), ok=pair([positive]), ok=pair([monotonic]),
    ok=pair([positive,monotonic,positive,monotonic]),
    P=erlang:unique_integer([positive,monotonic]), Q=erlang:unique_integer([monotonic,positive]),
    true=P>0, true=Q>P,
    M=erlang:unique_integer([monotonic]), N=erlang:unique_integer([monotonic,monotonic]), true=N>M,
    true=erlang:garbage_collect(), B.
next(_) -> integer_to_binary(erlang:unique_integer([monotonic])).
bad_type(B) -> erlang:unique_integer(B).
bad_option(B) -> erlang:unique_integer([positive,B]).
bad_tail(B) -> erlang:unique_integer([monotonic|B]).
family(B) ->
    Parent=self(), A=erlang:unique_integer([monotonic]),
    {Pid,Ref}=spawn_monitor(fun() -> Parent!{unique,erlang:unique_integer([monotonic])} end),
    receive {unique,V} -> true=V>A,
        receive {'DOWN',Ref,process,Pid,normal} ->
            C=erlang:unique_integer([monotonic]), true=C>V, B
        end
    end.
