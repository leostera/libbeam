%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(batch_alpha).
-export([run/0, entry/1]).
run() -> {43, batch_value}.
entry(Input) ->
    {43, batch_value} = run(),
    Input.
