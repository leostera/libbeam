-module(fib).
-export([main/1]).

main(Input) ->
    N = binary_to_integer(Input),
    integer_to_binary(fib(N)).

fib(0) -> 0;
fib(1) -> 1;
fib(N) ->
    Parent = self(),
    spawn(fun() -> Parent ! {left, fib(N - 1)} end),
    Right = fib(N - 2),
    receive {left, Left} -> Left + Right end.
