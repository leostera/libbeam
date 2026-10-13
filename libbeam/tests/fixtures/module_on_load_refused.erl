%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(module_on_load_refused).
-on_load(init/0).
-export([identity/1]).
init() -> ok.
identity(Input) -> Input.
