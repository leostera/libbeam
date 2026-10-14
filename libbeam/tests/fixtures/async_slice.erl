%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(async_slice).
-export([identity/1, loop/1, park/1, runnable/1, info/1, crash/1]).
-payload({a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,
          a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,
          a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,
          a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p}).
identity(Binary) -> Binary.
loop(_) -> ?MODULE:loop(ignored).
park(Binary) -> receive resume -> Binary end.
runnable(Binary) ->
    true = register(lifecycle_ready, self()),
    receive resume -> loop(Binary) end.
info(_) -> erlang:get_module_info(?MODULE).
crash(_) -> erlang:get_module_info(?MODULE, invalid_key).
