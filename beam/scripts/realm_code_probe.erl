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

%% W2 diagnostic, NOT a private-code implementation or acceptance test.
%% Run only in a disposable VM: deliberately replaces a global module.
-module(realm_code_probe).
-export([run/0, serve/2]).
-compile({no_inline, [apply_subject/2]}).

-define(SUBJECT, realm_code_probe_subject).

run() ->
    false = code:is_loaded(?SUBJECT),
    A = erts_internal:realm_create(#{restrict_process_access => true}),
    try
        B = erts_internal:realm_create(#{restrict_process_access => true}),
        try
            {module, ?SUBJECT} = load(a),
            {AIn, AOut} = start(A),
            {BIn, BOut} = start(B),
            Before = {sample(AIn, AOut), sample(BIn, BOut)},
            {module, ?SUBJECT} = load(b),
            After = {sample(AIn, AOut), sample(BIn, BOut)},
            ExpectedA = #{static => a, dynamic => a, external_fun => a, local_fun => a},
            ExpectedB = ExpectedA#{static => b, dynamic => b, external_fun => b},
            {ExpectedA, ExpectedA} = Before,
            {ExpectedB, ExpectedB} = After,
            #{decision => shared_code_only, before_reload => Before,
              after_reload => After, private_environment_acceptance => not_met}
        after
            erts_internal:realm_stop(B)
        end
    after
        erts_internal:realm_stop(A),
        %% Stop both roots before purging the old local fun's code.
        case code:is_loaded(?SUBJECT) of
            false -> ok;
            _ -> code:purge(?SUBJECT), code:delete(?SUBJECT), code:purge(?SUBJECT), ok
        end
    end.

load(Marker) ->
    Forms = [{attribute, 1, module, ?SUBJECT},
             {attribute, 2, export, [{value, 0}, {local_fun, 0}]},
             {function, 3, value, 0, [{clause, 3, [], [], [{atom, 3, Marker}]}]},
             {function, 4, local_fun, 0,
              [{clause, 4, [], [],
                [{'fun', 4, {clauses, [{clause, 4, [], [], [{atom, 4, Marker}]}]}}]}]}],
    {ok, ?SUBJECT, Binary} = compile:forms(Forms, [binary, report_errors, report_warnings]),
    code:load_binary(?SUBJECT, "realm_code_probe_subject.erl", Binary).

start(Realm) ->
    In = erts_internal:realm_endpoint_create(Realm, to_realm, 1, 1024),
    Out = erts_internal:realm_endpoint_create(Realm, to_host, 1, 1024),
    Pid = erts_internal:realm_spawn_root(Realm, ?MODULE, serve, [In, Out]),
    true = is_pid(Pid),
    {In, Out}.

serve(In, Out) ->
    loop(In, Out, fun realm_code_probe_subject:value/0,
         realm_code_probe_subject:local_fun()).

loop(In, Out, External, Local) ->
    case erts_internal:realm_endpoint_receive(In) of
        {ok, Command} ->
            {sample, Module, Function} = binary_to_term(Command, [safe]),
            Reply = #{static => realm_code_probe_subject:value(),
                      dynamic => apply_subject(Module, Function),
                      external_fun => External(), local_fun => Local()},
            ok = erts_internal:realm_endpoint_send(Out, term_to_binary(Reply)),
            loop(In, Out, External, Local);
        empty ->
            receive after 1 -> loop(In, Out, External, Local) end;
        closed -> ok
    end.

%% Decode operands from the endpoint: no_inline alone does not prevent the
%% compiler's interprocedural constant propagation from replacing apply.
apply_subject(Module, Function) -> apply(Module, Function, []).

sample(In, Out) ->
    ok = erts_internal:realm_endpoint_send(In, term_to_binary({sample, ?SUBJECT, value})),
    receive_sample(Out, erlang:monotonic_time(millisecond) + 5000).

receive_sample(Out, Deadline) ->
    case erts_internal:realm_endpoint_receive(Out) of
        {ok, Binary} -> binary_to_term(Binary, [safe]);
        empty ->
            case erlang:monotonic_time(millisecond) < Deadline of
                true -> receive after 1 -> receive_sample(Out, Deadline) end;
                false -> error(code_probe_timeout)
            end;
        closed -> error(code_probe_endpoint_closed)
    end.
