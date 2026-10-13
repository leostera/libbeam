%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(type_predicates_slice).
-export([bitstring_body/1, boolean_body/1, float_body/1, map_body/1, number_body/1,
         pid_body/1, port_body/1, reference_body/1, tuple_body/1, size_body/1,
         bitstring_guard/1, boolean_guard/1, float_guard/1, map_guard/1, number_guard/1,
         pid_guard/1, port_guard/1, reference_guard/1, tuple_guard/1, size_guard/1,
         bitstring_guard_y/2, boolean_guard_y/2, float_guard_y/2, map_guard_y/2,
         number_guard_y/2, pid_guard_y/2, port_guard_y/2, reference_guard_y/2,
         tuple_guard_y/2, size_guard_y/2, size_guard_bool/1, dynamic/2, identity/1]).
bitstring_body(T) -> is_bitstring(T).
boolean_body(T) -> is_boolean(T).
float_body(T) -> is_float(T).
map_body(T) -> is_map(T).
number_body(T) -> is_number(T).
pid_body(T) -> is_pid(T).
port_body(T) -> is_port(T).
reference_body(T) -> is_reference(T).
tuple_body(T) -> is_tuple(T).
size_body(T) -> tuple_size(T).
bitstring_guard(T) when is_bitstring(T) -> yes; bitstring_guard(_) -> no.
boolean_guard(T) when is_boolean(T) -> yes; boolean_guard(_) -> no.
float_guard(T) when is_float(T) -> yes; float_guard(_) -> no.
map_guard(T) when is_map(T) -> yes; map_guard(_) -> no.
number_guard(T) when is_number(T) -> yes; number_guard(_) -> no.
pid_guard(T) when is_pid(T) -> yes; pid_guard(_) -> no.
port_guard(T) when is_port(T) -> yes; port_guard(_) -> no.
reference_guard(T) when is_reference(T) -> yes; reference_guard(_) -> no.
tuple_guard(T) when is_tuple(T) -> yes; tuple_guard(_) -> no.
size_guard(T) when tuple_size(T) =:= 3 -> yes; size_guard(_) -> no.
size_guard_bool(T) when tuple_size(T) =:= 3 -> true; size_guard_bool(_) -> false.
bitstring_guard_y(T, X) -> R=identity(X), case is_bitstring(T) of true -> R; false -> no end.
boolean_guard_y(T, X) -> R=identity(X), case is_boolean(T) of true -> R; false -> no end.
float_guard_y(T, X) -> R=identity(X), case is_float(T) of true -> R; false -> no end.
map_guard_y(T, X) -> R=identity(X), case is_map(T) of true -> R; false -> no end.
number_guard_y(T, X) -> R=identity(X), case is_number(T) of true -> R; false -> no end.
pid_guard_y(T, X) -> R=identity(X), case is_pid(T) of true -> R; false -> no end.
port_guard_y(T, X) -> R=identity(X), case is_port(T) of true -> R; false -> no end.
reference_guard_y(T, X) -> R=identity(X), case is_reference(T) of true -> R; false -> no end.
tuple_guard_y(T, X) -> R=identity(X), case is_tuple(T) of true -> R; false -> no end.
size_guard_y(T, X) -> R=identity(X), case T of _ when tuple_size(T)=:=3 -> R; _ -> no end.
dynamic(Function, Term) -> apply(erlang, Function, [Term]).
identity(T) -> T.
