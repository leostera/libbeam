%% SPDX-License-Identifier: Apache-2.0
-module(batch_unsupported).
-export([run/1]).

run(_Value) -> #{first => 1, second => 2}.
