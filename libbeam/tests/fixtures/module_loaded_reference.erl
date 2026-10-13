%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(module_loaded_reference).
-export([run/0]).
run() ->
    {module,module_loaded_slice} = code:ensure_loaded(module_loaded_slice),
    true = module_loaded_slice:query(module_loaded_slice),
    true = module_loaded_slice:query(erlang),
    false = module_loaded_slice:query(unloaded_module_probe),
    false = module_loaded_slice:query(''),
    false = module_loaded_slice:query(true),
    false = module_loaded_slice:query(apply_target),
    {module,apply_target} = code:ensure_loaded(apply_target),
    true = module_loaded_slice:query(apply_target),
    true = code:delete(apply_target),
    false = module_loaded_slice:query(apply_target),
    _ = code:purge(apply_target),
    {module,apply_target} = code:ensure_loaded(apply_target),
    true = module_loaded_slice:query(apply_target),
    lists:foreach(fun(Value) ->
        try module_loaded_slice:query(Value) of Result -> erlang:error({expected_badarg,Value,Result})
        catch error:badarg -> ok end
    end,[[],0,{ok},[ok],1 bsl 60,1.0,<<>>,self()]),
    <<1,2,3>> = module_loaded_slice:copy(<<1,2,3>>),
    io:format("OTP_MODULE_LOADED_REFERENCE_OK current_code=true absent=false no_autoload=true unload_reload=true badarg=true~n").
