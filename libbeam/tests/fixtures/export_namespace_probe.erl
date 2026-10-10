%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(export_namespace_probe).
-export([version/0]).
-ifndef(VERSION).
-define(VERSION, 1).
-endif.
version() -> ?VERSION.
