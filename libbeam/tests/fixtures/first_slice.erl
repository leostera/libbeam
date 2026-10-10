%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(first_slice).
-export([value/0, identity/1, pair/1, unicode/0, literal/0]).

value() -> 42.
identity(X) -> X.
pair(X) -> {X, 42}.
unicode() -> 'λ🤖'.
literal() -> {<<"opaque literal payload">>, [a, b, c]}.
