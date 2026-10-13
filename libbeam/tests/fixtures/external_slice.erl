%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(external_slice).
-export([decode/1, identity/1, copy/1]).
decode(Binary) -> binary_to_term(Binary).
identity(Input) -> Input.
copy(Input) ->
    case decode(<<131,104,2,119,2,111,107,109,0,0,0,3,1,2,3>>) of
        {ok,<<1,2,3>>} -> Input
    end.
