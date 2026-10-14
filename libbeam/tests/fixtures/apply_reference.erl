%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(apply_reference).
-export([run/0]).
run() ->
    {41,42} = apply_host:list({apply_target,pair,[41,42]}),
    zero = apply_host:list({apply_target,zero,[]}),
    {41,42} = apply_host:list({erlang,apply,[erlang,apply,[apply_target,pair,[41,42]]]}),
    lists:foreach(fun(Name) ->
        {42} = apply(apply_host,Name,[{apply_target,one,[{42}]}]),
        badarg(fun() -> apply(apply_host,Name,[{[],one,[{42}]}]) end)
    end,[list,list_last,external]),
    {applied,{42}} = apply_host:list_call({apply_target,one,[{42}]}),
    {42} = apply_host:fixed({apply_target,one,{42}}),
    {applied,{42}} = apply_host:fixed_call({apply_target,one,{42}}),
    42 = apply_host:list({erlang,abs,[-42]}),
    lists:foreach(fun(Triple) -> badarg(fun() -> apply_host:list(Triple) end) end,
        [{[],one,[]},{apply_target,42,[]},{apply_target,one,42},
         {apply_target,one,[1|bad_tail]}, {apply_target,one,improper(255)},
         {erlang,apply,[[],one,[]]}]),
    badarg(fun() -> apply_host:fixed({{bad_module},one,{42}}) end),
    badarg(fun() -> apply_host:fixed_call({apply_target,[],{42}}) end),
    try apply_host:list({apply_target,one,improper(1024)}) of _ -> error(expected_system_limit)
    catch error:system_limit -> ok end,
    %% Installed OTP 28 and the pinned beam/ source differ here. Measure this
    %% boundary separately; the native test follows beam/erl_vm.h MAX_ARG=255.
    Boundary = try apply_host:list({apply_target,one,improper(256)}) of _ -> unexpected_success
               catch error:Reason -> Reason end,
    io:format("OTP_APPLY_256_IMPROPER_OBSERVED otp=~s reason=~p pinned_source_reason=system_limit~n",
        [erlang:system_info(otp_release),Boundary]),
    <<"payload">> = apply_host:copy(<<"payload">>),
    <<"payload">> = apply_host:fun_copy(<<"payload">>),
    Fun=fun apply_target:one/1,
    lists:foreach(fun(Name) ->
        {42}=apply(apply_host,Name,[{Fun,[{42}]}]),
        badarg(fun() -> apply(apply_host,Name,[{not_a_fun,improper(2)}]) end),
        try apply(apply_host,Name,[{not_a_fun,[]}]) of _ -> error(expected_badfun)
        catch error:{badfun,not_a_fun} -> ok end
    end,[fun_list,fun_last,fun_external,fun_dynamic]),
    {applied,{42}}=apply_host:fun_call({Fun,[{42}]}),
    lists:foreach(fun(Count) ->
        Args=lists:seq(0,Count-1),
        try apply_host:fun_list({Fun,Args}) of _ -> error(expected_badarity)
        catch error:{badarity,{Fun,Args}} -> ok end
    end,[0,2,255,256,1022]),
    %% Pinned beam_common.c accepts MAX_REG-1 arguments; installed JIT reserves
    %% another register. Keep this difference visible rather than normalizing it.
    FunBoundary=try apply_host:fun_list({Fun,lists:duplicate(1023,0)}) of _ -> unexpected_success
                catch error:{badarity,_} -> badarity; error:Why -> Why end,
    true=(FunBoundary=:=badarity orelse FunBoundary=:=system_limit),
    io:format("OTP_APPLY_FUN_1023_OBSERVED otp=~s reason=~p pinned_source_reason=badarity~n",
        [erlang:system_info(otp_release),FunBoundary]),
    lists:foreach(fun(Tail) ->
        Args=lists:foldl(fun(_,A)->[0|A] end,Tail,lists:seq(1,1024)),
        try apply_host:fun_list({Fun,Args}) of _ -> error(expected_system_limit)
        catch error:system_limit -> ok end
    end,[[],bad_tail]),
    io:format("OTP_APPLY_REFERENCE_OK list_fixed_nested=true external=true native_target=true badarg_system_limit=true~n").
improper(N) -> lists:foldl(fun(_,Tail) -> [0|Tail] end,bad_tail,lists:seq(1,N)).
badarg(F) -> try F() of _ -> error(expected_badarg) catch error:badarg -> ok end.
