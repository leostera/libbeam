%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(pid_slice).
-export([identity/0, pair/0, classify/1, echo/1]).
identity() -> self().
pair() -> {self(), identity()}.
classify(P) when is_pid(P) -> <<"pid">>;
classify(_) -> <<"not_pid">>.
echo(B) ->
    P = identity(),
    case is_pid(P) of true -> B; false -> <<"not_pid">> end.
