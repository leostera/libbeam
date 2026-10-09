%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>

%% Trusted full-OTP bring-up fixture, not a reduced-profile tenant module.
-module(engine_start_probe).
-export([run/0]).

run() ->
    #{status := standalone_startup_passed} = startup_probe:run(),
    no_executable_ports(),
    io:format("BEAM_STARTUP_OK pid=~s~n", [os:getpid()]),
    %% Do not call halt/0 or init:stop/0: either would kill the C++ host.
    ok.

no_executable_ports() ->
    Ports = lists:sort(erlang:ports()),
    false = lists:any(fun(P) -> erlang:port_info(P, name) == {name, "forker"} end,
                      Ports),
    %% If a regression permits execution, it can only write the test driver's
    %% private sentinel. Both missing BINDIR and a canary helper are tested too.
    Command = "printf EXECUTED > \"$LIBBEAM_PROBE_EXEC_SENTINEL\"",
    Requests = [{{spawn, Command}, []},
                {{spawn_executable, "/bin/sh"}, [{args, ["-c", Command]}]}],
    lists:foreach(fun({Name, Options}) ->
        notsup = erts_internal:open_port(Name, Options),
        expect_notsup(fun() -> erlang:open_port(Name, Options) end),
        expect_notsup(fun() -> apply(erlang, open_port, [Name, Options]) end)
    end, Requests),
    try os:cmd(Command) of
        Result -> error({os_cmd_was_not_denied, Result})
    catch
        error:badarg:Stack ->
            [{os, cmd, _, Info} | _] = Stack,
            {error_info, #{cause := {open_port, notsup}}} =
                lists:keyfind(error_info, 1, Info)
    end,
    Ports = lists:sort(erlang:ports()),
    io:format("EXECUTABLE_PORTS_DENIED checks=7 forker_port=false~n").

expect_notsup(Fun) ->
    try Fun() of
        Unexpected -> error({executable_port_was_not_denied, Unexpected})
    catch
        error:notsup -> ok
    end.
