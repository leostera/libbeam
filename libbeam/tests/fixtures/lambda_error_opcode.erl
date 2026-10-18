% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(lambda_error_opcode).
-export([entry/1]).
entry(_Input) -> fun captured/0.
captured() -> <<"ok">>.
