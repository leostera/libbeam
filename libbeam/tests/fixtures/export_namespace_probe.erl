%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(export_namespace_probe).
-export([version/0, closure/1, record_make/1, record_get/1, catch_value/0, literal/0]).
-export_record([stamp]).
-record #stamp{value=0}.
-ifndef(VERSION).
-define(VERSION, 1).
-endif.
version() -> ?VERSION.
literal() -> {owner_literal, [1,2,3], <<"retained literal binary">>}.
closure(N) -> fun(X) -> X + N + ?VERSION end.
record_make(N) -> #stamp{value=N}.
record_get(R) -> R#stamp.value.
catch_value() ->
    try erlang:error(?VERSION)
    catch error:N:Stack -> {N, Stack}
    end.
