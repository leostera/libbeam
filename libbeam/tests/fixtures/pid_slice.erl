%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(pid_slice).
-export([identity/0, pair/0, classify/1, echo/1, reference/0, reference_pair/0,
         reference_echo/1, reference_classify/1, reference_equal/2,
         invoke/2, invoke0/1, captured_pair/2]).
invoke(F,A) -> F(A).
invoke0(F) -> F().
captured_pair(A,B) -> {A,B}.
reference_equal(A,B) when A =:= B -> <<"equal">>;
reference_equal(_,_) -> <<"different">>.
reference() -> make_ref().
reference_pair() -> {reference(),reference()}.
reference_echo(B) ->
    R = reference(),
    case is_reference(R) of true -> B; false -> <<"not_ref">> end.
reference_classify(R) when is_reference(R) -> <<"ref">>;
reference_classify(_) -> <<"not_ref">>.
identity() -> self().
pair() -> {self(), identity()}.
classify(P) when is_pid(P) -> <<"pid">>;
classify(_) -> <<"not_pid">>.
echo(B) ->
    P = identity(),
    case is_pid(P) of true -> B; false -> <<"not_pid">> end.
