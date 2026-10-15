%% SPDX-License-Identifier: Apache-2.0
%% OTP-compiled gc_bif bor/2 lowers to i_bor_jssd on the pinned profile.
-module(bor_slice).
-export([run/1, big/1, bad/1]).

run(<<Left:8, Right:8>>) -> <<(Left bor Right):8>>.

big(Bytes) when is_binary(Bytes) ->
    <<(((1 bsl 70) bor byte_size(Bytes))):128>>.

bad(Bytes) -> Bytes bor 1.
