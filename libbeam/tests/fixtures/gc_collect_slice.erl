%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(gc_collect_slice).
-export([identity/1,copy/1]).
identity(B) -> B.
copy(B) ->
    Root = {B,[B],fun() -> B end},
    undefined = put(gc_root,Root),
    true = garbage_collect(),
    {B,[B],F} = erase(gc_root),
    B = F(),
    B.
