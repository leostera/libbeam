%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(refused_map).
-export([identity/1, unused_map/1]).
identity(Binary) -> Binary.
%% An unused unsupported function still refuses the entire module.
unused_map(Binary) -> #{body => Binary}.
