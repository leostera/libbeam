% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(nif_start_opcode).
-nifs([native/1]).
-export([entry/1,identity/1,native/1,init/0]).
entry(_Input) -> native(<<"ok">>).
init() -> erlang:load_nif("libnif_start_probe",0).
native(Input) -> Input.
identity(Input) -> Input.
