%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(boolean_slice).
-export([identity/1,copy/1,conjunction/2,disjunction/2,exclusive/2,dynamic/3,
         guard_and/2,guard_or/2,guard_xor/2,window5/5,bad_and/1,bad_or/1,bad_xor/1]).
identity(B) -> B.
conjunction(A,B) -> A and B.
disjunction(A,B) -> A or B.
exclusive(A,B) -> A xor B.
dynamic(F,A,B) -> apply(erlang,F,[A,B]).
guard_and(A,B) when (A and B) =:= true -> true;
guard_and(A,B) when (A and B) =:= false -> false;
guard_and(_,_) -> invalid.
guard_or(A,B) when (A or B) =:= true -> true;
guard_or(A,B) when (A or B) =:= false -> false;
guard_or(_,_) -> invalid.
guard_xor(A,B) when (A xor B) =:= true -> true;
guard_xor(A,B) when (A xor B) =:= false -> false;
guard_xor(_,_) -> invalid.
copy(B) ->
    true = erlang:garbage_collect(),
    ok = verify(false,false,false,false,false),
    ok = verify(false,true,false,true,true),
    ok = verify(true,false,false,true,true),
    ok = verify(true,true,true,true,false), B.
verify(A,B,And,Or,Xor) ->
    And = conjunction(A,B), Or = disjunction(A,B), Xor = exclusive(A,B),
    And = dynamic('and',A,B), Or = dynamic('or',A,B), Xor = dynamic('xor',A,B),
    And = guard_and(A,B), Or = guard_or(A,B), Xor = guard_xor(A,B), ok.
window5(A,B,C,D,E) -> true = erlang:garbage_collect(), {A,B,C,D,E}.
bad_and(B) -> conjunction(false,B).
bad_or(B) -> disjunction(true,B).
bad_xor(B) -> exclusive(B,false).
