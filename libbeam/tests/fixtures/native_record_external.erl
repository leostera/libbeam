% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(native_record_external).
-export([entry/1, record/1]).
record(_Input) -> #native_record_public:thing{first=one}.
entry(_Input) ->
    R = #native_record_public:thing{first=one},
    case R#native_record_public:thing.first of
        one -> <<"ok">>;
        _ -> <<"wrong">>
    end.
