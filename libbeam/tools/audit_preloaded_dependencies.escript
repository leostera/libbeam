#!/usr/bin/env escript
%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Trusted source inspection only: never compiles or executes guest functions.
-mode(compile).

main([Source]) ->
    {ok, Forms} = epp:parse_file(Source, [], []),
    Errors = [E || E = {error, _} <- Forms],
    case Errors of
        [] -> ok;
        _ -> erlang:error({source_parse_errors, Errors})
    end,
    Functions = [function(F) || F = {function, _, _, _, _} <- Forms],
    Report = #{scope => <<"AST source dependencies, not executable coverage">>,
               otp_release => list_to_binary(erlang:system_info(otp_release)),
               erts_version => list_to_binary(erlang:system_info(version)),
               functions => Functions},
    io:put_chars(json:encode(Report)),
    io:nl();
main(_) ->
    erlang:error({usage, "audit_preloaded_dependencies.escript SOURCE"}).

function({function, Line, Name, Arity, Clauses}) ->
    #{name => atom_to_binary(Name), arity => Arity, line => erl_anno:line(Line),
      nif_stub => lists:all(fun nif_stub_clause/1, Clauses),
      static_calls => lists:usort(calls(Clauses)),
      guard_calls => lists:usort(lists:flatmap(
          fun({clause, _, _, Guards, _}) -> calls(Guards) end, Clauses)),
      static_fun_references => lists:usort(fun_references(Clauses)),
      operators => lists:usort(operators(Clauses)),
      dynamic_call_count => dynamic_calls(Clauses)}.

nif_stub_clause({clause, _, _, [], [{call, _,
                  {remote, _, {atom, _, erlang}, {atom, _, nif_error}}, _}]}) -> true;
nif_stub_clause(_) -> false.

calls({call, Line, {remote, _, {atom, _, Module}, {atom, _, Name}}, Args}) ->
    [#{kind => remote, module => atom_to_binary(Module), name => atom_to_binary(Name),
       arity => length(Args), line => erl_anno:line(Line)} | calls(Args)];
calls({call, Line, {atom, _, Name}, Args}) ->
    [#{kind => local_or_autoimport, name => atom_to_binary(Name), arity => length(Args),
       line => erl_anno:line(Line)} | calls(Args)];
calls(Tuple) when is_tuple(Tuple) -> calls(tuple_to_list(Tuple));
calls(List) when is_list(List) -> lists:flatmap(fun calls/1, List);
calls(_) -> [].

fun_references({'fun', Line, {function, {atom, _, Module}, {atom, _, Name}, {integer, _, Arity}}}) ->
    [#{kind => remote, module => atom_to_binary(Module), name => atom_to_binary(Name),
       arity => Arity, line => erl_anno:line(Line)}];
fun_references({'fun', Line, {function, Name, Arity}}) when is_atom(Name), is_integer(Arity) ->
    [#{kind => local, name => atom_to_binary(Name), arity => Arity, line => erl_anno:line(Line)}];
fun_references(Tuple) when is_tuple(Tuple) -> fun_references(tuple_to_list(Tuple));
fun_references(List) when is_list(List) -> lists:flatmap(fun fun_references/1, List);
fun_references(_) -> [].

operators({op, _, Op, Arg}) -> [atom_to_binary(Op) | operators(Arg)];
operators({op, _, Op, Left, Right}) -> [atom_to_binary(Op) | operators([Left, Right])];
operators(Tuple) when is_tuple(Tuple) -> operators(tuple_to_list(Tuple));
operators(List) when is_list(List) -> lists:flatmap(fun operators/1, List);
operators(_) -> [].

dynamic_calls({call, _, {remote, _, {atom, _, _}, {atom, _, _}}, Args}) -> dynamic_calls(Args);
dynamic_calls({call, _, {atom, _, _}, Args}) -> dynamic_calls(Args);
dynamic_calls({call, _, Callee, Args}) -> 1 + dynamic_calls([Callee, Args]);
dynamic_calls(Tuple) when is_tuple(Tuple) -> dynamic_calls(tuple_to_list(Tuple));
dynamic_calls(List) when is_list(List) -> lists:sum([dynamic_calls(X) || X <- List]);
dynamic_calls(_) -> 0.
