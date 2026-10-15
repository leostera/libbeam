%% SPDX-License-Identifier: Apache-2.0
-module(batch_unsupported).
-on_load(init/0).
-export([run/1]).

init() -> ok.
run(Value) -> Value.
