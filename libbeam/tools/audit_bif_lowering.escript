#!/usr/bin/env escript
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Compile reference fixtures and inspect them; never execute their functions.
-mode(compile).

main([Inventory, Output]) ->
    {ok, Data} = file:read_file(Inventory),
    #{<<"entries">> := Entries} = json:decode(Data),
    Rows = lists:zip(lists:seq(1, length(Entries)), Entries),
    Fixtures = lists:flatmap(fun fixtures/1, Rows),
    Source = filename:join(Output, "bif_batch_lowering.erl"),
    ok = filelib:ensure_dir(Source),
    Exports = lists:join(",", [io_lib:format("~p/~B", [F, A]) || {F,A,_,_} <- Fixtures]),
    ok = file:write_file(Source, ["-module(bif_batch_lowering).\n-export([", Exports, "]).\n",
                                  [Body || {_,_,Body,_} <- Fixtures]]),
    {ok, bif_batch_lowering, Beam, _Warnings} = compile:file(Source, [binary, return_errors, return_warnings]),
    BeamFile = filename:join(Output, "bif_batch_lowering.beam"),
    ok = file:write_file(BeamFile, Beam),
    {beam_file, bif_batch_lowering, _, _, _, Functions} = beam_disasm:file(BeamFile),
    ByFunction = maps:from_list([{{F,A}, Instructions} || {function,F,A,_,Instructions} <- Functions]),
    Results = [Metadata#{fixture => atom_to_binary(F), fixture_arity => A,
                        instructions => [iolist_to_binary(io_lib:format("~p", [I]))
                                         || I <- maps:get({F,A}, ByFunction)]}
               || {F,A,_,Metadata} <- Fixtures],
    io:put_chars(json:encode(#{scope => <<"Reference compiler lowering only; no guest execution">>,
                              otp_release => list_to_binary(erlang:system_info(otp_release)),
                              erts_version => list_to_binary(erlang:system_info(version)),
                              entries => Results})),
    io:nl();
main(_) -> erlang:error({usage, "audit_bif_lowering.escript INVENTORY.json EXTERNAL_OUTPUT_DIRECTORY"}).

fixtures({Index, #{<<"function">> := Function, <<"arity">> := Arity}}) ->
    Name = binary_to_atom(Function),
    Args = lists:join(",", [io_lib:format("A~B", [N]) || N <- lists:seq(1,Arity)]),
    Expr = io_lib:format("erlang:~p(~s)", [Name, Args]),
    BodyName = list_to_atom("body_" ++ integer_to_list(Index)),
    Metadata = #{module => <<"erlang">>, function => Function, arity => Arity},
    Body = {BodyName, Arity, io_lib:format("~p(~s) -> ~s.\n", [BodyName, Args, Expr]),
            Metadata#{form => body}},
    GuardAllowed = erl_internal:guard_bif(Name, Arity) orelse
                   erl_internal:arith_op(Name, Arity) orelse
                   erl_internal:bool_op(Name, Arity),
    DynamicName = list_to_atom("dynamic_" ++ integer_to_list(Index)),
    Dynamic = {DynamicName, 1,
               io_lib:format("~p(Args) -> erlang:apply(erlang, ~p, Args).~n", [DynamicName, Name]),
               Metadata#{form => dynamic}},
    Predicate = case GuardAllowed andalso lists:prefix("is_", atom_to_list(Name)) of
        false -> [];
        true ->
            PredicateName = list_to_atom("predicate_" ++ integer_to_list(Index)),
            PredicateBody = io_lib:format("~p(~s) when ~s -> yes;~n~p(~s) -> no.~n",
                                         [PredicateName, Args, Expr, PredicateName, Args]),
            [{PredicateName, Arity, PredicateBody, Metadata#{form => predicate_test}}]
    end,
    Operators = case {Name, Arity} of
        {send, 2} ->
            SendName = list_to_atom("send_operator_" ++ integer_to_list(Index)),
            [{SendName, 2, io_lib:format("~p(A1,A2) -> A1 ! A2.~n", [SendName]),
              Metadata#{form => operator}}];
        _ -> []
    end,
    case GuardAllowed of
        false -> [Body, Dynamic | Operators];
        true ->
            GuardName = list_to_atom("guard_" ++ integer_to_list(Index)),
            GuardArgs = case Arity of 0 -> "Expected"; _ -> [Args, ",Expected"] end,
            GuardBody = io_lib:format("~p(~s) when ~s =:= Expected -> true;~n~p(~s) -> false.~n",
                                     [GuardName, GuardArgs, Expr, GuardName, GuardArgs]),
            [Body, Dynamic, {GuardName, Arity+1, GuardBody, Metadata#{form => guard}} | Predicate ++ Operators]
    end.
