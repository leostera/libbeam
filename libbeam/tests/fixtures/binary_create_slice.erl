%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Loader-generator witness only; this module remains execution-unadmitted.
-module(binary_create_slice).
-export([mixed/5, string/1, append/2, private/1, sized/2]).
mixed(B, I, F, C, S) ->
    <<B/bitstring, I:S/little-signed, F:32/float-little,
      C/utf8, C/utf16-native, C/utf32-big>>.
string(I) -> <<"native string descriptor", I:7>>.
append(B, I) -> <<B/binary, I:8>>.
private(B) -> << <<I>> || <<I>> <= B >>.
sized(B, N) -> <<B:N/binary>>.
