%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(fun_target).
-export([captured_pair/2]).
captured_pair(A,B) ->
    R = make_ref(),
    {A,B,R}.
