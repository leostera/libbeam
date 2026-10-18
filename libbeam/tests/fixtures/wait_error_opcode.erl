% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(wait_error_opcode).
-export([entry/1,identity/1]).
entry(_Input) ->
    true = (try receive after -1 -> ignored end
            catch error:timeout_value -> true end),
    true = (try receive foo -> ignored after -1 -> ignored end
            catch error:timeout_value -> true end),
    <<"ok">>.
identity(Input) -> Input.
