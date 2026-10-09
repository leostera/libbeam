%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>

%% Trusted full-OTP bring-up fixture, not a reduced-profile tenant module.
-module(engine_start_probe).
-export([run/0]).

run() ->
    #{status := standalone_startup_passed} = startup_probe:run(),
    io:format("BEAM_STARTUP_OK pid=~s~n", [os:getpid()]),
    %% Do not call halt/0 or init:stop/0: either would kill the C++ host.
    ok.
