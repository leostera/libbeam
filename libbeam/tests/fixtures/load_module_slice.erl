%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(load_module_slice).
-export([direct/2, dynamic/2, decoded_dynamic/2, present/1, absent/1]).
direct(Module, Code) -> erlang:load_module(Module, Code).
dynamic({Module, Function}, Code) -> apply(erlang, Function, [Module, Code]).
decoded_dynamic(Module, Code) ->
    Function = binary_to_term(<<131,119,11,"load_module">>),
    apply(erlang, Function, [Module, Code]).
present(Input) -> {module, ?MODULE} = erlang:load_module(?MODULE, Input), Input.
absent(Code) -> erlang:load_module(load_module_absent, Code).
