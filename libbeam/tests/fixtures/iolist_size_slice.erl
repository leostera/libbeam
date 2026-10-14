%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(iolist_size_slice).
-export([identity/1,copy/1,cases/1,repeat/2,bad_head/1,bad_negative/1,bad_tail/1,bad_bits/1]).
identity(B) -> B.
copy(B) ->
    Io = [[],0,[B,[255]],<<1,2>>,B|<<3>>],
    Expected = 2 * byte_size(B) + 5,
    Expected = erlang:iolist_size(Io),
    Expected = apply(erlang,iolist_size,[Io]),
    true = erlang:garbage_collect(),
    Expected = erlang:iolist_size(Io),
    B.
cases(B) ->
    B = copy(B),
    N = byte_size(B), N = erlang:iolist_size(nest(512,B)),
    0 = erlang:iolist_size(nest(512,[])),
    Huge = <<0:8388608>>,
    %% The logical size exceeds the owned binary limit. Counting must not
    %% materialize the repeated bytes or apply iolist_to_binary's size cap.
    68157440 = erlang:iolist_size(repeat(65,Huge)),
    B.
nest(0,B) -> B;
nest(N,B) -> nest(N-1,[B]).
repeat(0,_) -> [];
repeat(N,B) -> [B|repeat(N-1,B)].
bad_head(B) -> erlang:iolist_size([B,256]).
bad_negative(B) -> erlang:iolist_size([-1,B]).
bad_tail(B) -> erlang:iolist_size([B|3]).
bad_bits(B) -> erlang:iolist_size([B,<<1:1>>]).
