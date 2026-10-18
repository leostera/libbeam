% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(call_error_handler_opcode).
-export([entry/1,identity/1,uncaught/1]).
entry(_Input) ->
    try missing_error_handler_target:missing_function(17)
    catch error:undef -> <<"ok">> end.
identity(Input) -> Input.
uncaught(_Input) -> missing_error_handler_target:missing_function(17).
