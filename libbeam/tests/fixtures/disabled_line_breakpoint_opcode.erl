% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(disabled_line_breakpoint_opcode).
-export([entry/1,identity/1,disabled_line_breakpoint/1]).
entry(Input) ->
    {Function,Mode,Value} = erlang:binary_to_term(Input),
    Result = case {Function,Mode} of
        {disabled_line_breakpoint,direct} -> disabled_line_breakpoint(Value);
        {disabled_line_breakpoint,dynamic} -> erlang:apply(?MODULE,Function,[Value])
    end,
    case Result of 7 -> <<"ok">>; _ -> <<"failed">> end.
identity(Input) -> Input.
disabled_line_breakpoint(Value) -> Value.
