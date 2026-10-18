% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(call_light_bif_opcode).
-export([entry/1,identity/1,put_bif/0]).
entry(_Input) ->
    undefined = erlang:put(call_light_bif_key,call_light_bif_value),
    undefined = put_bif(),
    <<"ok">>.
put_bif() -> erlang:put(call_light_bif_tail_key,call_light_bif_tail_value).
identity(Input) -> Input.
