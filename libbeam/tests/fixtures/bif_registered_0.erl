% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_registered_0).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[]) -> erlang:'registered'();
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    true = register(bif_owned_name,self()), Names = invoke(F, Mode, []), true = unregister(bif_owned_name), has(bif_owned_name,Names).
has(X,[X|_]) -> true; has(X,[_|Xs]) -> has(X,Xs); has(_,[]) -> false.
