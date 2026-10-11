%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Proof/preparation fixture; executable binary construction remains refused.
-module(binary_flow_slice).
-export([make/1, concat/2, append/2]).
make(I) -> <<I:8>>.
concat(B, I) -> <<0:1, B/bitstring, I:7>>.
append(B, I) -> <<B/bitstring, I:7>>.
