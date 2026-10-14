%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(gc_roots).
-export([identity/1, collect/1, resume/1, registers_stack/1, dictionary/1,
         mailbox/1, closures/1, binary_views/1, pending_exception/1,
         timer_payload/1, alias_reference/1, hibernate_zero/1, hibernate_mfa/1,
         wake/1, make_box/1]).
identity(Input) -> Input.
collect(_) -> gc_probe ! collect, <<"collect">>.
resume(_) -> gc_probe ! resume, <<"resume">>.

prepare() ->
    true = register(gc_probe, self()),
    put(phase, before_gc),
    receive collect -> ok end.
barrier() ->
    prepare(),
    true = erlang:garbage_collect(),
    %% Replacing an existing atom/atom dictionary entry needs no heap cells.
    put(phase, after_gc),
    receive resume -> ok end.

make_box(Input) ->
    {Input, [Input, {Input}], #{payload => Input}, (1 bsl 130) + byte_size(Input)}.
registers_stack(Input) ->
    Box = ?MODULE:make_box(Input),
    barrier(),
    {Result, [Result, {Result}], #{payload := Result}, Big} = Box,
    Big = (1 bsl 130) + byte_size(Result),
    Result.
dictionary(Input) ->
    put(payload, {kept, Input}),
    barrier(),
    {kept, Result} = erase(payload),
    Result.
mailbox(Input) ->
    self() ! {kept, Input},
    barrier(),
    receive {kept, Result} -> Result end.
closures(Input) ->
    Local = fun() -> Input end,
    External = fun ?MODULE:identity/1,
    barrier(),
    Input = Local(),
    External(Input).
binary_views(Input) ->
    Size = byte_size(Input) - 2,
    <<_:8, Sub:Size/binary, _:8>> = Input,
    <<_:3, Unaligned:4097/bitstring, _/bitstring>> = Input,
    put(sub, Sub),
    put(bits, Unaligned),
    barrier(),
    Sub = erase(sub),
    Unaligned = erase(bits),
    <<_:8, Sub:Size/binary, _:8>> = Input,
    <<_:3, Unaligned:4097/bitstring, _/bitstring>> = Input,
    Input.
pending_exception(Input) ->
    try
        try error({kept, Input})
        after barrier() end
    catch
        error:{kept, Result}:Stack ->
            [{?MODULE, pending_exception, 1, _} | _] = Stack,
            Result
    end.
timer_payload(Input) ->
    Ref = erlang:start_timer(1000, self(), {kept, Input}),
    barrier(),
    true = is_integer(erlang:read_timer(Ref)),
    receive {timeout, Ref, {kept, Result}} -> Result end.
alias_reference(Input) ->
    Alias = erlang:alias(),
    barrier(),
    Alias ! {kept, Input},
    receive
        {kept, Result} ->
            true = erlang:unalias(Alias),
            Result
    end.
hibernate_zero(Input) ->
    prepare(),
    erlang:hibernate(),
    receive resume -> Input end.
hibernate_mfa(Input) ->
    prepare(),
    erlang:hibernate(?MODULE, wake, [Input]).
wake(Input) -> receive resume -> Input end.
