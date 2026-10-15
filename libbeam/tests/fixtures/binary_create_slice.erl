%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Generator and strict private-append proof witness; execution is checked separately.
-module(binary_create_slice).
-export([mixed/5, string/1, append/2, private/1, sized/2, literal/1]).
mixed(B, I, F, C, S) ->
    <<B/bitstring, I:S/little-signed, F:32/float-little,
      C/utf8, C/utf16-native, C/utf32-big>>.
string(I) -> <<"native string descriptor", I:7>>.
append(B, I) -> <<B/binary, I:8>>.
private(B) -> << <<I>> || <<I>> <= B >>.
sized(B, N) -> <<B:N/binary>>.
literal(N) -> <<(1 bsl 64):N/little>>.
