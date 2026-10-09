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

%% Disposable-VM measurements, never a security or density acceptance gate.
-module(realm_bench).
-export([run/2, measured_root/2, ready/1]).

run(Mode, N) when (Mode =:= host orelse Mode =:= restricted), N >= 100, N =< 10000 ->
    erlang:garbage_collect(),
    Before = erlang:memory(total),
    Measurements = case Mode of
                       host -> workloads(host, N);
                       restricted -> in_realm(measured_root, N)
                   end,
    After = erlang:memory(total),
    Activation = case Mode =:= host andalso erlang:function_exported(erts_internal, realm_create, 1) of
                     true -> measure(realm_activation_stop, min(N, 100),
                                     fun() -> in_realm(ready, 0), ok end);
                     false -> #{name => realm_activation_stop, supported => false}
                 end,
    #{mode => Mode, iterations => N, otp => erlang:system_info(otp_release),
      system_version => erlang:system_info(system_version),
      architecture => erlang:system_info(system_architecture),
      build_type => erlang:system_info(build_type), flavor => erlang:system_info(emu_flavor),
      schedulers => erlang:system_info(schedulers_online),
      vm_memory_before => Before, vm_memory_after => After,
      measurements => Measurements ++ [Activation]}.

workloads(Mode, N) ->
    [measure(clock_pair, N, fun() -> ok end),
     measure(spawn_exit, N, fun() ->
                                   {P, R} = spawn_monitor(fun() -> ok end),
                                   receive {'DOWN', R, process, P, normal} -> ok end
                           end),
     ping(small_message, N, <<0:64>>, pid),
     ping(large_binary_message, N, <<0:(65536*8)>>, pid),
     ping(alias_message, N, <<0:64>>, alias),
     ping(priority_message, N, <<0:64>>, priority),
     registry(Mode, N), ets_measure(N), density(min(N, 100))] ++ mutable_resources(N).

measure(Name, N, Operation) ->
    lists:foreach(fun(_) -> Operation() end, lists:seq(1, min(20, N))),
    Start = erlang:monotonic_time(nanosecond),
    Samples = [begin
                   T = erlang:monotonic_time(nanosecond),
                   Operation(),
                   erlang:monotonic_time(nanosecond) - T
               end || _ <- lists:seq(1, N)],
    Total = erlang:monotonic_time(nanosecond) - Start,
    Sorted = lists:sort(Samples),
    #{name => Name, supported => true, samples => N, total_ns => Total,
      p50_ns => lists:nth((N*50+99) div 100, Sorted),
      p99_ns => lists:nth((N*99+99) div 100, Sorted),
      min_ns => hd(Sorted), max_ns => lists:last(Sorted)}.

ping(Name, N, Payload, Kind) ->
    Reply = case Kind of pid -> self(); alias -> alias(); priority -> alias([priority]) end,
    {P, Ref} = spawn_monitor(fun Echo() ->
        receive
            {To, Bytes} ->
                case Kind of priority -> erlang:send(To, Bytes, [priority]); _ -> To ! Bytes end,
                Echo();
            stop -> ok
        end
    end),
    try measure(Name, N, fun() -> P ! {Reply, Payload}, receive Payload -> ok end end)
    after
        P ! stop,
        receive {'DOWN', Ref, process, P, normal} -> ok end,
        case Kind of pid -> ok; _ -> unalias(Reply) end
    end.

registry(restricted, _N) -> #{name => registration, supported => false,
                             reason => temporarily_denied_by_profile};
registry(host, N) ->
    measure(registration, N, fun() ->
        true = register(realm_bench_registered, self()),
        Self = self(), Self = whereis(realm_bench_registered),
        true = unregister(realm_bench_registered)
    end).

ets_measure(N) ->
    Table = ets:new(realm_bench, [set, private]),
    try measure(private_ets, N, fun() ->
        true = ets:insert(Table, {key, value}),
        [{key, value}] = ets:lookup(Table, key),
        true = ets:delete(Table, key)
    end)
    after ets:delete(Table) end.

mutable_resources(N) ->
    A = atomics:new(1, []),
    Atomic = measure(atomic_add_get, N, fun() -> atomics:add_get(A, 1, 1) end),
    Counters = [begin
        C = counters:new(1, [Type]),
        measure(Name, N, fun() -> counters:add(C, 1, 1), counters:get(C, 1) end)
    end || {Type, Name} <- [{atomics, counter_atomic_add_get},
                           {write_concurrency, counter_write_add_get}]],
    [Atomic | Counters].

density(N) ->
    Parent = self(),
    Workers = [spawn_monitor(fun() -> Parent ! {self(), ready}, receive stop -> ok end end)
               || _ <- lists:seq(1, N)],
    try
        lists:foreach(fun({P, _}) -> receive {P, ready} -> ok end end, Workers),
        Bytes = lists:sum([begin {memory, M} = process_info(P, memory), M end || {P, _} <- Workers]),
        #{name => process_memory, supported => true, processes => N,
          process_info_memory_sum => Bytes}
    after
        lists:foreach(fun({P, R}) -> P ! stop, receive {'DOWN', R, process, P, normal} -> ok end end, Workers)
    end.

in_realm(Function, N) ->
    Handle = realm:create(#{restrict_process_access => true}),
    try
        Out = realm:endpoint_create(Handle, to_host, 1, 65536),
        Arguments = case Function of ready -> [Out]; _ -> [Out, N] end,
        _ = realm:spawn_root(Handle, ?MODULE, Function, Arguments),
        await(Out, erlang:monotonic_time(millisecond) + 30000)
    after realm:stop(Handle) end.

measured_root(Out, N) ->
    ok = realm:endpoint_send(Out, term_to_binary(workloads(restricted, N))).
ready(Out) -> ok = realm:endpoint_send(Out, term_to_binary(ready)).

await(Out, Deadline) ->
    case realm:endpoint_receive(Out) of
        {ok, Binary} -> binary_to_term(Binary, [safe]);
        empty ->
            case erlang:monotonic_time(millisecond) < Deadline of
                true -> receive after 1 -> await(Out, Deadline) end;
                false -> error(benchmark_timeout)
            end;
        closed -> error(benchmark_endpoint_closed)
    end.
