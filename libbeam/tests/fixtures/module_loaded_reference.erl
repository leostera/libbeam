%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(module_loaded_reference).
-export([run/0]).
run() ->
    {module,module_loaded_slice} = code:ensure_loaded(module_loaded_slice),
    true = module_loaded_slice:query(module_loaded_slice),
    true = module_loaded_slice:query(erlang),
    %% Stock OTP has additional boot modules; the owned runtime boots erlang.
    true = lists:member(erlang,erlang:pre_loaded()),
    Loaded = erlang:loaded(),
    true = lists:member(erlang,Loaded),
    true = lists:member(module_loaded_slice,Loaded),
    true = length(Loaded) =:= length(lists:usort(Loaded)),
    false = lists:member(apply_target,Loaded),
    false = module_loaded_slice:query(unloaded_module_probe),
    false = module_loaded_slice:query(''),
    false = module_loaded_slice:query(true),
    false = module_loaded_slice:query(apply_target),
    {module,apply_target} = code:ensure_loaded(apply_target),
    true = module_loaded_slice:query(apply_target),
    Snapshot = erlang:loaded(),
    true = lists:member(apply_target,Snapshot),
    true = code:delete(apply_target),
    false = module_loaded_slice:query(apply_target),
    %% loaded/0 includes old code in OTP until purge; owned unloading has no
    %% old-code generation and removes the module in one serialized step.
    _ = code:purge(apply_target),
    false = lists:member(apply_target,erlang:loaded()),
    true = erlang:garbage_collect(),
    true = lists:member(apply_target,Snapshot),
    {module,apply_target} = code:ensure_loaded(apply_target),
    true = module_loaded_slice:query(apply_target),
    lists:foreach(fun(Value) ->
        try module_loaded_slice:query(Value) of Result -> erlang:error({expected_badarg,Value,Result})
        catch error:badarg -> ok end
    end,[[],0,{ok},[ok],1 bsl 60,1.0,<<>>,self()]),
    <<1,2,3>> = module_loaded_slice:copy(<<1,2,3>>),
    io:format("OTP_MODULE_LOADED_REFERENCE_OK current_code=true absent=false no_autoload=true unload_reload=true badarg=true~n").
