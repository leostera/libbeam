%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>

%% Trusted bootstrap bring-up fixture, not a reduced-profile tenant module.
-module(engine_start_probe).
-export([run/0, float_div/2]).

run() ->
    #{status := standalone_startup_passed} = startup_probe:run(),
    fd_eof(),
    no_executable_ports(),
    no_signal_administration(),
    atom_storage(),
    export_literals(),
    export_tables(),
    3.75 = float_div(7.5, 2.0),
    1.25 = binary_to_float(<<"1.25">>),
    <<"1.250">> = float_to_binary(1.25, [{decimals, 3}]),
    try float_div(1.0, 0.0) of
        Value -> error({expected_badarith, Value})
    catch error:badarith -> ok end,
    io:format("BEAM_STARTUP_OK pid=~s~n", [os:getpid()]),
    %% Do not call halt/0 or init:stop/0: either would kill the C++ host.
    ok.

%% The driver supplies /dev/null as stdin. Exercise the retained FD driver's
%% EOF and close paths after removing its shared spawn-handshake machinery.
fd_eof() ->
    Port = erlang:open_port({fd, 0, 0}, [in, binary, eof]),
    Ref = erlang:monitor(port, Port),
    receive {Port, eof} -> ok after 5000 -> error(fd_eof_timeout) end,
    true = erlang:port_close(Port),
    receive {'DOWN', Ref, port, Port, normal} -> ok
    after 5000 -> error(fd_close_timeout) end.

no_executable_ports() ->
    Ports = lists:sort(erlang:ports()),
    false = lists:any(fun(P) -> erlang:port_info(P, name) == {name, "forker"} end,
                      Ports),
    %% If a regression permits execution, it can only write the test driver's
    %% private sentinel. Both missing BINDIR and a canary helper are tested too.
    Command = "printf EXECUTED > \"$LIBBEAM_PROBE_EXEC_SENTINEL\"",
    Requests = [{{spawn, Command}, []},
                {{spawn_executable, "/bin/sh"}, [{args, ["-c", Command]}]},
                {{spawn_driver, "ram_file_drv"}, []}],
    lists:foreach(fun({Name, Options}) ->
        notsup = erts_internal:open_port(Name, Options),
        expect_notsup(fun() -> erlang:open_port(Name, Options) end),
        expect_notsup(fun() -> apply(erlang, open_port, [Name, Options]) end)
    end, Requests),
    false = erlang:function_exported(os, cmd, 1),
    false = erlang:function_exported(os, cmd, 2),
    expect_undef(fun() -> os:cmd(Command) end),
    expect_undef(fun() -> os:cmd(Command, #{}) end),
    Ports = lists:sort(erlang:ports()),
    io:format("EXECUTABLE_PORTS_DENIED checks=11 forker_port=false~n").

export_tables() ->
    Dir = filename:dirname(code:which(?MODULE)),
    {ok, A} = file:read_file(filename:join(Dir, "export_namespace_probe.beam")),
    {ok, B} = file:read_file(filename:join([Dir, "export-v2", "export_namespace_probe.beam"])),
    F = erlang:make_fun(export_namespace_probe, version, 0),
    Parent = self(),
    Refs = [begin
                Ref = make_ref(),
                spawn(fun() ->
                    lists:foreach(fun(_) ->
                        G = erlang:make_fun(libbeam_missing_export, probe, 0),
                        {module, libbeam_missing_export} = erlang:fun_info(G, module)
                    end, lists:seq(1, 500)),
                    Parent ! {Ref, done}
                end),
                Ref
            end || _ <- lists:seq(1, 16)],
    lists:foreach(fun(_) ->
        true = code:soft_purge(export_namespace_probe),
        {module, export_namespace_probe} = code:load_binary(export_namespace_probe, "v1", A),
        1 = F(),
        true = code:soft_purge(export_namespace_probe),
        {module, export_namespace_probe} = code:load_binary(export_namespace_probe, "v2", B),
        2 = F()
    end, lists:seq(1, 4)),
    lists:foreach(fun(Ref) ->
        receive {Ref, done} -> ok after 5000 -> error(export_stub_timeout) end
    end, Refs),
    true = code:soft_purge(export_namespace_probe),
    true = code:delete(export_namespace_probe),
    true = code:soft_purge(export_namespace_probe),
    1 = F(), % The same external fun resolves again through autoload.
    io:format("EXPORT_TABLE_OK reload_and_stub_lookup=true private_execution=false~n").

export_literals() ->
    F = fun lists:reverse/1,
    F = erlang:make_fun(lists, reverse, 1),
    Abs = fun erlang:abs/1,
    {F, Abs} = binary_to_term(term_to_binary({F, Abs})),
    true = erlang:garbage_collect(),
    [3,2,1] = F([1,2,3]),
    3 = Abs(-3),
    {module, lists} = erlang:fun_info(F, module),
    "fun lists:reverse/1" = erlang:fun_to_list(F),
    Parent = self(),
    spawn(fun() -> Parent ! {external_fun_result, F([1,2])} end),
    receive {external_fun_result, [2,1]} -> ok
    after 5000 -> error(external_fun_timeout) end,
    io:format("EXPORT_LITERAL_OK gc_roundtrip_and_dispatch=true~n").

atom_storage() ->
    Names = [<<>>, <<"libbeam_atom_copy">>, <<0>>, <<255/utf8>>,
             <<16#1f600/utf8>>, binary:copy(<<16#1f600/utf8>>, 255)],
    lists:foreach(fun(Bytes) ->
        Atom = binary_to_atom(Bytes, utf8),
        Bytes = atom_to_binary(Atom, utf8),
        Bytes = atom_to_binary(Atom, unicode),
        true = erlang:garbage_collect(),
        Bytes = atom_to_binary(Atom, utf8)
    end, Names),
    Latin = binary_to_atom(<<0,255>>, latin1),
    <<0,255>> = atom_to_binary(Latin, latin1),
    <<0,255/utf8>> = atom_to_binary(Latin, utf8),
    [<<>>, <<"a">>, <<>>, <<>>] = binary:split(<<",a,,">>, <<",">>, [global]),
    [<<"a">>] = binary:split(<<",a,,">>, <<",">>, [global, trim_all]),
    io:format("ATOM_STORAGE_OK copied_names=true empty_binary=true~n").

float_div(A, B) -> A / B.

no_signal_administration() ->
    false = erlang:is_builtin(os, set_signal, 2),
    false = erlang:function_exported(os, set_signal, 2),
    lists:foreach(fun(Name) -> undefined = whereis(Name) end,
                  [erl_signal_server, inet_db, net_sup, net_kernel, rex,
                   global_name_server, global_group, erl_epmd, dist_ac,
                   kernel_safe_sup, erl_compile_server]),
    Expected = lists:sort([code_server, standard_error, file_server_2, on_load,
                           user, logger_sup, kernel_config, kernel_refc]),
    Expected = lists:sort([Id || {Id, _, _, _} <- supervisor:which_children(kernel_sup)]),
    lists:foreach(fun(Mode) ->
        expect_undef(fun() -> os:set_signal(sigusr1, Mode) end),
        expect_undef(fun() -> apply(os, set_signal, [sigusr1, Mode]) end)
    end, [ignore, default, handle]),
    io:format("SIGNAL_ADMIN_REMOVED checks=6 signal_server=false~n").

expect_undef(Fun) ->
    try Fun() of
        Unexpected -> error({removed_api_was_present, Unexpected})
    catch error:undef -> ok end.

expect_notsup(Fun) ->
    try Fun() of
        Unexpected -> error({executable_port_was_not_denied, Unexpected})
    catch
        error:notsup -> ok
    end.
