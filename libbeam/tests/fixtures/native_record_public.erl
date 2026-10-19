% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(native_record_public).
-export_record([thing]).
-record #thing{second = two, first = one}.
-export([create/1]).
create(First) -> #thing{first=First}.
