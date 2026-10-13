%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(refused_exception).
-export([identity/1, unused_exception/1]).
identity(Binary) -> Binary.
%% Whole-module admission must reject this even when only identity is invoked.
unused_exception(Function) ->
    try Function() of
        Value -> Value
    catch
        _:_ -> refused
    end.
