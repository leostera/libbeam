%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(bif_dist_ctrl_put_data_2).
-export([entry/1, identity/1]).
entry(_) -> erlang:dist_ctrl_put_data([], []).
identity(Input) -> Input.
