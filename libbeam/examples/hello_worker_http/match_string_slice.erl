%% SPDX-License-Identifier: Apache-2.0
%% Preserve an OTP-compiled string match across aligned and unaligned cursors.
-module(match_string_slice).
-export([run/1]).

run(<<"abcdefghijklmnop", Rest/binary>>) -> Rest;
run(<<0:1, "qrstuvwxyzABCDEFGHIJ", _Tail:7>>) -> <<"unaligned">>;
run(_) -> <<"miss">>.
