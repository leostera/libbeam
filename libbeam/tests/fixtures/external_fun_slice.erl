-module(external_fun_slice).
-export([make/0, call/0]).

make() -> fun erlang:self/0.
call() -> (fun erlang:self/0)().
