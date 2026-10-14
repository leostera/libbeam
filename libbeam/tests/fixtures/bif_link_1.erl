% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_link_1).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0]) -> erlang:'link'(A0);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    P = spawn(fun() -> receive stop -> ok end end), true = invoke(F, Mode, [P]), {links,L} = erlang:process_info(self(),links), true = has(P,L), true = unlink(P), P ! stop, true.
has(X,[X|_]) -> true; has(X,[_|Xs]) -> has(X,Xs); has(_,[]) -> false.
