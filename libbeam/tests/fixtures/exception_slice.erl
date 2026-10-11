%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(exception_slice).
-export([badmatch/1, case_clause/1, function_clause/1, bif/1, identity/1]).
badmatch(Value) -> expected = Value, <<>>.
case_clause(Value) -> case Value of expected -> <<>> end.
function_clause(expected) -> <<>>.
bif(Value) -> integer_to_binary(Value).
identity(Value) -> Value.
