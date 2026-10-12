%% SPDX-License-Identifier: Apache-2.0
-module(batch_alpha).
-export([run/0, entry/1, identity/1, base/0]).

run() -> batch_beta:beta().
entry(Binary) -> batch_beta:echo(Binary).
identity(Binary) -> Binary.
base() -> {42, batch_value}.
