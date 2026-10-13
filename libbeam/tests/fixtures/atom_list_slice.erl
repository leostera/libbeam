%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(atom_list_slice).
-export([convert/1, latin1/1, utf8/1, identity/1, copy/1]).
convert(Atom) -> atom_to_list(Atom).
latin1(Atom) -> atom_to_binary(Atom, latin1).
utf8(Atom) -> atom_to_binary(Atom, utf8).
identity(Input) -> Input.
copy(Input) ->
    case convert('aé€😀') of
        [97,233,8364,128512] -> Input
    end.
