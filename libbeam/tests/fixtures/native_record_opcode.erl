% SPDX-License-Identifier: Apache-2.0
% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(native_record_opcode).
-record #thing{second = two, first = one}.
-export([entry/1,identity/1,native_record/1,update/1,field/1,second/1]).
entry(_Input) ->
    Original = native_record(one),
    Updated = update(Original),
    case {field(Original), second(Updated)} of
        {one, three} -> <<"ok">>;
        _ -> <<"wrong">>
    end.
identity(Input) -> Input.
native_record(First) -> #thing{first=First}.
update(Record) -> Record#thing{second=three}.
field(Record) -> Record#thing.first.
second(Record) -> Record#thing.second.
