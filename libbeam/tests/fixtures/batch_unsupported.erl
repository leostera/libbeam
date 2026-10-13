%% SPDX-License-Identifier: Apache-2.0
-module(batch_unsupported).
-export([run/1]).

%% Maps are now admitted. Keep the rollback probe on exception control,
%% which must remain refused until unwinding and stacktraces are implemented.
run(Function) ->
    try Function() of
        Value -> Value
    catch
        _:_ -> unsupported
    end.
