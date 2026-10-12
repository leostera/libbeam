%% SPDX-License-Identifier: Apache-2.0
-module(batch_beta).
-export([beta/0, echo/1]).

beta() -> batch_alpha:base().
echo(Binary) -> batch_alpha:identity(Binary).
