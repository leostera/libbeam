-module(bs_match_equality_slice).
-export([match/1]).
match(<<Prefix:1, 16#1122334455667788:64, _/bitstring>>) -> {yes, Prefix};
match(_) -> no.
