%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(function_exported_reference).
-export([run/0]).
run() ->
    {module,function_exported_slice} = code:ensure_loaded(function_exported_slice),
    {module,apply_target} = code:ensure_loaded(apply_target),
    lists:foreach(fun(Query) -> true = function_exported_slice:query(Query) end,
        [{function_exported_slice,identity,1},{apply_target,zero,0},{apply_target,one,1},
         {erlang,abs,1},{erlang,apply,3},{apply_target,one,4294967297},{apply_target,one,-4294967295}]),
    lists:foreach(fun(Query) -> false = function_exported_slice:query(Query) end,
        [{function_exported_slice,private,1},{apply_target,one,0},{apply_target,missing,1},
         {unloaded_probe_module,missing,1},{apply_target,one,-1},{apply_target,one,256}]),
    lists:foreach(fun(Query) ->
        try function_exported_slice:query(Query) of _ -> error(expected_badarg)
        catch error:badarg -> ok end
    end,[{[],one,1},{apply_target,[],1},{apply_target,one,1.0},{apply_target,one,1 bsl 60}]),
    false = code:is_loaded(unloaded_probe_module),
    <<"payload">> = function_exported_slice:copy(<<"payload">>),
    io:format("OTP_FUNCTION_EXPORTED_REFERENCE_OK loaded_native_instruction=true missing_private=false arity_wrap=true badarg=true no_autoload=true~n").
