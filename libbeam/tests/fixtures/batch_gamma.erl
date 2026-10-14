%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(batch_gamma).
-export([run/0, entry/1]).
run() -> batch_alpha:run().
entry(Input) ->
    {42, batch_value} = run(),
    Input.
