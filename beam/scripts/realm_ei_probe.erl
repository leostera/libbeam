%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%

%% Standalone initial-handshake reproducer, without Common Test or spawn tests.
%% Requires the repaired fwd_node helper (newline-delimited actual node name).
-module(realm_ei_probe).
-export([run/2]).

run(Executable, Mode) when Mode =:= guessed; Mode =:= actual ->
    true = node() =/= nonode@nohost,
    OldTrap = process_flag(trap_exit, true),
    true = register(cnode_forward_receiver, self()),
    Name = "realm_ei_probe_" ++ integer_to_list(erlang:system_time(microsecond)),
    Args = ["-sname", Name, "-cookie", atom_to_list(erlang:get_cookie()),
            "-creation", "4"],
    try
        Port = open_port({spawn_executable, filename:absname(Executable)},
                         [use_stdio, exit_status, {line, 1024}, {args, Args}]),
        try
            receive
                {Port, {data, {eol, "accepting " ++ Actual}}} ->
                    [_Alive, Host] = string:split(atom_to_list(node()), "@"),
                    Guessed = Name ++ "@" ++ Host,
                    io:format("guessed=~s actual=~s mode=~p~n", [Guessed, Actual, Mode]),
                    Destination = list_to_atom(case Mode of guessed -> Guessed; actual -> Actual end),
                    Ref = make_ref(),
                    {probe_target, Destination} ! Ref,
                    receive
                        Ref -> ok;
                        {Port, {exit_status, Status}} -> {error, {c_node_exit, Status}}
                    after 5000 -> {error, reply_timeout}
                    end;
                {Port, {exit_status, Status}} -> {error, {startup_exit, Status}}
            after 10000 -> {error, startup_timeout}
            end
        after
            try port_close(Port) catch error:badarg -> ok end
        end
    after
        unregister(cnode_forward_receiver),
        process_flag(trap_exit, OldTrap)
    end.
