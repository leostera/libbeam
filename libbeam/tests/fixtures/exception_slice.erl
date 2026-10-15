%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(exception_slice).
-export([badmatch/1, case_clause/1, function_clause/1, bif/1, badrecord/1, if_end/1, exiting/1, identity/1]).
-record(sample, {value}).
badmatch(Value) -> expected = Value, <<>>.
case_clause(Value) -> case Value of expected -> <<>> end.
function_clause(expected) -> <<>>.
bif(Value) -> integer_to_binary(Value).
badrecord(Value) -> Value#sample.value.
if_end(Value) -> if Value == expected -> <<>> end.
exiting(Value) -> exit(Value).
identity(Value) -> Value.
