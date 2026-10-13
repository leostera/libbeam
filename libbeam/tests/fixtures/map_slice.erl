-module(map_slice).
-export([new/1, add/2, update/2, fetch/1, empty/0, new_binary/1, add_binary/2, update_binary/2, add_created_binary/1, update_created_binary/1, single_created_binary/1, dynamic_binary/1, exact_missing/1]).

new(Value) -> #{alpha => Value, beta => 7}.
add(Map, Value) ->
    case is_map(Map) of
        true -> Map#{gamma => Value};
        false -> invalid
    end.
update(Map, Value) ->
    case is_map(Map) of
        true -> Map#{alpha := Value};
        false -> invalid
    end.
fetch(Map) ->
    case Map of
        #{alpha := A, beta := B} -> {A, B};
        _ -> missing
    end.
empty() -> #{}.
new_binary(Value) ->
    case new(Value) of
        #{alpha := Value, beta := 7} -> <<"map-ok">>;
        _ -> <<"map-bad">>
    end.
add_binary(Map, Value) ->
    case add(Map, Value) of
        #{gamma := Value} -> <<"assoc-ok">>;
        _ -> <<"assoc-bad">>
    end.
update_binary(Map, Value) ->
    case update(Map, Value) of
        #{alpha := Value} -> <<"exact-ok">>;
        _ -> <<"exact-bad">>
    end.
add_created_binary(Value) ->
    case add(new(Value), Value) of
        #{alpha := Value, beta := 7, gamma := Value} -> <<"assoc-ok">>;
        _ -> <<"assoc-bad">>
    end.
update_created_binary(Value) ->
    case update(new(Value), Value) of
        #{alpha := Value, beta := 7} -> <<"exact-ok">>;
        _ -> <<"exact-bad">>
    end.
single_created_binary(Value) ->
    case add(new(Value), Value) of
        #{gamma := Value} -> <<"single-ok">>;
        _ -> <<"single-bad">>
    end.
dynamic_binary(Key) ->
    case #{Key => 7} of
        #{Key := 7} -> <<"dynamic-ok">>;
        _ -> <<"dynamic-bad">>
    end.
exact_missing(Value) -> update(#{}, Value).
