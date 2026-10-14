%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_list_slice).
-export([identity/1, copy/1]).
identity(B) -> B.
copy(B) -> iolist_to_binary(binary_to_list(B)).
