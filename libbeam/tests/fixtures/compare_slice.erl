%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(compare_slice).
-export([same/2, different/2, command/1, not_command/1, nilish/1, literal/1, not_literal/1, empty/1]).
empty(<<>>) -> <<"empty">>.
same(A, B) when A =:= B -> <<"equal">>;
same(_, _) -> <<"different">>.
different(A, B) when A =/= B -> <<"different">>;
different(_, _) -> <<"equal">>.
command(Binary) when Binary =:= <<"next">> -> <<"next-ok">>;
command(_) -> <<"other">>.
not_command(Binary) when Binary =/= <<"next">> -> <<"other">>;
not_command(_) -> <<"next-ok">>.
nilish(Value) when Value =:= [] -> <<"nil">>;
nilish(_) -> <<"other">>.
literal(Value) when Value =:= {{{{{{{{7,0},1},2},3},4},5},6},7} -> <<"equal">>;
literal(_) -> <<"different">>.
not_literal(Value) when Value =/= {{{{{{{{7,0},1},2},3},4},5},6},7} -> <<"different">>;
not_literal(_) -> <<"equal">>.
