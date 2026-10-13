%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(process_info_slice).
-export([query/1, other/1, identity/1, copy/1]).
query(Selector) -> process_info(self(),Selector).
other({Pid,Selector}) -> process_info(Pid,Selector).
identity(Input) -> Input.
copy(Input) ->
    case {query(error_handler),query([error_handler,error_handler]),query([])} of
        {{error_handler,error_handler},[{error_handler,error_handler},{error_handler,error_handler}],[]} -> Input
    end.
