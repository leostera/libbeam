%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(map_key_slice).
-export([present/2, guarded/2, flat/1, empty/1, large/1, guard_large/1, badmap/1, identity/1, copy/1,
         cardinality/1, size_empty/1, size_flat/1, size_large/1, guard_size/1, guard_size_large/1]).
present(Key, Map) -> is_map_key(Key, Map).
guarded(Key, Map) when is_map_key(Key, Map) -> yes;
guarded(_, _) -> no.
flat(Key) -> present(Key, #{1 => one, 2 => two}).
empty(Key) -> present(Key, #{}).
large(Key) -> present(Key, large_map()).
guard_large(Key) -> guarded(Key, large_map()).
badmap(Value) -> present(1, Value).
large_map() ->
    #{1=>v,2=>v,3=>v,4=>v,5=>v,6=>v,7=>v,8=>v,9=>v,10=>v,11=>v,
      12=>v,13=>v,14=>v,15=>v,16=>v,17=>v,18=>v,19=>v,20=>v,21=>v,22=>v,
      23=>v,24=>v,25=>v,26=>v,27=>v,28=>v,29=>v,30=>v,31=>v,32=>v,33=>v}.
cardinality(Map) -> map_size(Map).
size_empty(_) -> cardinality(#{}).
size_flat(_) -> cardinality(#{1=>v,2=>v}).
size_large(_) -> cardinality(large_map()).
guard_size(Map) when map_size(Map) =:= 33 -> yes;
guard_size(_) -> no.
guard_size_large(_) -> guard_size(large_map()).
identity(Input) -> Input.
copy(Input) ->
    case {flat(1), flat(99), large(33), large(99), size_empty(0), size_flat(0), size_large(0)} of
        {true, false, true, false, 0, 2, 33} -> Input
    end.
