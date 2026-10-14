%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bit_size_slice).
-export([direct/1,dynamic/1,invoke/2,guarded/1,kept/1,copy/1,bad/1,identity/1]).
identity(B) -> B.
direct(B) -> bit_size(B).
dynamic(B) -> invoke(bit_size,B).
invoke(F,B) -> apply(erlang,F,[B]).
guarded(B) when bit_size(B)=:=bit_size(B) -> bit_size(B);
guarded(_) -> no.
kept(B) -> Size=bit_size(B), true=erlang:garbage_collect(), {Size,B}.
copy(B) -> Size=byte_size(B)*8, Size=direct(B), Size=dynamic(B), Size=guarded(B),
    true=kept(B)=:={Size,B}, B.
bad(B) -> bit_size([B]).
