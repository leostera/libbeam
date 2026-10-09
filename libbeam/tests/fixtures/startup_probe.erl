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

%% Standalone OTP startup regression witness, NOT a tenant-profile fixture.
-module(startup_probe).
-export([run/0]).

run() ->
    Init = whereis(init),
    true = is_pid(Init),
    true = erts_internal:is_system_process(Init),
    {initial_call, {erl_init, start, 2}} = process_info(Init, initial_call),
    Expected = lists:sort([{erts_code_purger, high},
                           {erts_literal_area_collector, high},
                           {erts_dirty_process_signal_handler, normal},
                           {erts_dirty_process_signal_handler, high},
                           {erts_dirty_process_signal_handler, max},
                           {erts_trace_cleaner, normal}]),
    Modules = [M || {M, _} <- Expected],
    Found = [{Pid, M} || Pid <- processes(),
                        {initial_call, {M, start, 0}} <- [process_info(Pid, initial_call)],
                        lists:member(M, Modules)],
    Expected = lists:sort([begin
        true = erts_internal:is_system_process(Pid),
        {message_queue_data, off_heap} = process_info(Pid, message_queue_data),
        {priority, Priority} = process_info(Pid, priority),
        {M, Priority}
    end || {Pid, M} <- Found]),
    Parent = self(),
    [begin
        {Pid, Ref} = spawn_monitor(fun() -> Parent ! {self(), N} end),
        receive {Pid, N} -> ok after 5000 -> error(spawn_timeout) end,
        receive {'DOWN', Ref, process, Pid, normal} -> ok
        after 5000 -> error(monitor_timeout) end
    end || N <- lists:seq(1, 20)],
    #{status => standalone_startup_passed, housekeeping_processes => length(Found),
      spawn_monitor_cycles => 20, build_type => erlang:system_info(build_type),
      flavor => erlang:system_info(emu_flavor), isolate_acceptance => not_implemented}.
