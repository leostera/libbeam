% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_md5_update_2).
-export([entry/1,identity/1,invoke/3,check/3]).
entry(Input) ->
    {F,Mode,V} = erlang:binary_to_term(Input),
    case check(F,Mode,V) of true -> <<"ok">>; _ -> <<"contract mismatch">> end.
identity(Input) -> Input.
invoke(_F,direct,[A0,A1]) -> erlang:'md5_update'(A0,A1);
invoke(F,_DynamicMode,Args) -> erlang:apply(erlang,F,Args).
check(F,Mode,V) ->
    C = invoke(F, Mode, [erlang:md5_init(),[V+90,V+91,V+92]]), erlang:md5_final(C) =:= <<16#900150983cd24fb0d6963f7d28e17f72:128>>.
