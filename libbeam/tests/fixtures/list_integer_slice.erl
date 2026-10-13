%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(list_integer_slice).
-export([parse/1, binary_parse/1, identity/1, copy/1]).
parse(List) -> list_to_integer(List).
binary_parse(Binary) -> binary_to_integer(Binary).
identity(Input) -> Input.
copy(Input) ->
    case parse("-18446744073709551616") of
        -18446744073709551616 -> Input
    end.
