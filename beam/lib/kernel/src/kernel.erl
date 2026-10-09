%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 1996-2025. All Rights Reserved.
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
%%
-module(kernel).
-moduledoc false.
-behaviour(supervisor).
-export([start/2, init/1, stop/1]).

%% Temporary trusted bootstrap for libbeam. There is only one startup contract:
%% no node/distribution, hostname discovery, signal server, peer controller,
%% remote boot server, or optional administration-service startup.
%% File/code loading, on_load and console/logging are still bring-up dependencies,
%% not permission for tenant bytecode to use those effects.
start(_, []) ->
    ok = logger:internal_init_logger(),
    case supervisor:start_link({local, kernel_sup}, kernel, []) of
        {ok, Pid} ->
            ok = logger:add_handlers(kernel),
            {ok, Pid, []};
        Error -> Error
    end.

stop(_State) ->
    ok.

init([]) ->
    SupFlags = #{strategy => one_for_all, intensity => 0, period => 1},
    Config = #{id => kernel_config,
               start => {kernel_config, start_link, []},
               restart => permanent,
               shutdown => 2000,
               type => worker,
               modules => [kernel_config]},
    RefC = #{id => kernel_refc,
             start => {kernel_refc, start_link, []},
             restart => permanent,
             shutdown => 2000,
             type => worker,
             modules => [kernel_refc]},
    Code = #{id => code_server,
             start => {code, start_link, []},
             restart => permanent,
             shutdown => 2000,
             type => worker,
             modules => [code]},
    File = #{id => file_server_2,
             start => {file_server, start_link, []},
             restart => permanent,
             shutdown => 2000,
             type => worker,
             modules => [file, file_server, file_io_server, prim_file]},
    StdError = #{id => standard_error,
                 start => {standard_error, start_link, []},
                 restart => temporary,
                 shutdown => 2000,
                 type => supervisor,
                 modules => [standard_error]},
    OnLoad = #{id => on_load,
               start => {proc_lib, start_link, [?MODULE, init, [on_load]]},
               restart => transient,
               shutdown => 2000,
               type => worker,
               modules => [?MODULE]},
    User = #{id => user,
             start => {user_sup, start, []},
             restart => temporary,
             shutdown => 2000,
             type => supervisor,
             modules => [user_sup]},
    LoggerSup = #{id => logger_sup,
                  start => {logger_sup, start_link, []},
                  restart => permanent,
                  shutdown => infinity,
                  type => supervisor,
                  modules => [logger_sup]},
    {ok, {SupFlags, [Code, StdError, File, OnLoad, User, LoggerSup, Config, RefC]}};
init(on_load) ->
    init:run_on_load_handlers(),
    proc_lib:init_ack({ok, self()}).
