%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_parts_slice).
-export([head/1, tail/1, guarded_head/1, guarded_tail/1, guarded_head_y/2, guarded_tail_y/2, identity/1, copy/1]).
head(Value) -> hd(Value).
tail(Value) -> tl(Value).
guarded_head(Value) when hd(Value) =:= 7 -> yes;
guarded_head(_) -> no.
guarded_tail(Value) when tl(Value) =:= done -> yes;
guarded_tail(_) -> no.
guarded_head_y(Value, Other) ->
    Result = identity(Other),
    if hd(Value) =:= 7 -> Result; true -> no end.
guarded_tail_y(Value, Other) ->
    Result = identity(Other),
    if tl(Value) =:= done -> Result; true -> no end.
identity(Input) -> Input.
copy(Input) ->
    Input = head([Input]),
    Input = tail([prefix | Input]),
    case {guarded_head([7 | done]), guarded_tail([7 | done])} of
        {yes, yes} -> Input
    end.
