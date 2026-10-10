%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(binary_slice).
-export([identity/1, wrap/1, drop/1, literal/0, bits/0, attributes/1, all_info/1]).
-payload(<<0:2048, 16#7F>>).

identity(Binary) -> Binary.
wrap(Binary) -> {Binary, Binary}.
drop(_) -> discarded.
attributes(_) -> erlang:get_module_info(?MODULE, attributes).
all_info(_) -> erlang:get_module_info(?MODULE).
%% Compound constants exercise ETF literal admission, not bs_create_bin (which
%% is a separate instruction-family dependency). These must compile as literals.
literal() -> {<<"0123456789012345678901234567890123456789"
                 "0123456789012345678901234567890123456789">>}.
bits() -> {<<"0123456789012345678901234567890123456789"
              "0123456789012345678901234567890123456789", 5:3>>}.
