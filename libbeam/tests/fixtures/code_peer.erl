%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(code_peer).
-export([forward/0, local/0, loop/0, wide/1, branch/2]).
-stress_attribute({[], {}, 1.25, <<5:3>>, 18446744073709551616}).

forward() -> first_slice:literal().
local() -> ?MODULE:wide(ok).
loop() -> ?MODULE:loop().
branch(A, B) -> {A, case B of 0 -> zero; _ -> nonzero end}.
wide(X) ->
    {X,X,X,X,X,X,X,X, X,X,X,X,X,X,X,X,
     X,X,X,X,X,X,X,X, X,X,X,X,X,X,X,X,
     X,X,X,X,X,X,X,X, X,X,X,X,X,X,X,X,
     X,X,X,X,X,X,X,X, X,X,X,X,X,X,X,X}.
