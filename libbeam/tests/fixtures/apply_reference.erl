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
    io:format("OTP_APPLY_REFERENCE_OK list_fixed_nested=true external=true native_target=true badarg_system_limit=true~n").
improper(N) -> lists:foldl(fun(_,Tail) -> [0|Tail] end,bad_tail,lists:seq(1,N)).
badarg(F) -> try F() of _ -> error(expected_badarg) catch error:badarg -> ok end.
