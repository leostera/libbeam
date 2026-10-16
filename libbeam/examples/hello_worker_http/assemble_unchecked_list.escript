#!/usr/bin/env escript
%% SPDX-License-Identifier: Apache-2.0
%% Assemble the deliberately unguarded yxx fixture without OTP's validator;
%% only a test of libbeam's native checked dispatch may execute this BEAM.
main([Source, Target]) -> assemble(Source, Target, y);
main([Source, Target, "x"]) -> assemble(Source, Target, x);
main([Source, Target, "rxr"]) -> assemble(Source, Target, rxr);
main([Source, Target, "tl"]) -> assemble(Source, Target, tl).

assemble(Source, Target, Mode) ->
    {ok, Forms0} = file:consult(Source),
    Forms = case Mode of
        y -> Forms0;
        x -> lists:flatmap(fun
            ({get_list, {y,0}, {x,1}, {x,2}}) ->
                %% Keep the prefix live in x3 while x0 becomes the list source;
                %% restore it before the next GC-capable operation.
                [{move,{x,0},{x,3}}, {move,{y,0},{x,0}},
                 {get_list,{x,0},{x,1},{x,2}}, {move,{x,3},{x,0}}];
            (Instruction) -> [Instruction]
        end, Forms0);
        rxr -> lists:flatmap(fun
            ({get_list, {y,0}, {x,1}, {x,2}}) ->
                [{move,{x,0},{x,3}}, {move,{y,0},{x,0}},
                 {get_list,{x,0},{x,1},{x,0}}, {move,{x,0},{x,2}},
                 {move,{x,3},{x,0}}];
            (Instruction) -> [Instruction]
        end, Forms0);
        tl -> lists:flatmap(fun
            ({get_list, {y,0}, {x,1}, {x,2}}) ->
                [{move,{integer,65},{x,1}}, {get_tl,{y,0},{x,2}}];
            (Instruction) -> [Instruction]
        end, Forms0)
    end,
    {module, Mod} = lists:keyfind(module, 1, Forms),
    {exports, Exports} = lists:keyfind(exports, 1, Forms),
    {attributes, Attributes} = lists:keyfind(attributes, 1, Forms),
    {labels, Labels} = lists:keyfind(labels, 1, Forms),
    {Done, {Name, Arity, Label, Ops}} = lists:foldl(fun
        ({module, _}, Acc) -> Acc;
        ({exports, _}, Acc) -> Acc;
        ({attributes, _}, Acc) -> Acc;
        ({labels, _}, Acc) -> Acc;
        ({function, NewName, NewArity, NewLabel}, {Functions, none}) ->
            {Functions, {NewName, NewArity, NewLabel, []}};
        ({function, NewName, NewArity, NewLabel}, {Functions, {OldName, OldArity, OldLabel, OldOps}}) ->
            {[{function, OldName, OldArity, OldLabel, lists:reverse(OldOps)} | Functions],
             {NewName, NewArity, NewLabel, []}};
        (Instruction, {Functions, {CurrentName, CurrentArity, CurrentLabel, CurrentOps}}) ->
            {Functions, {CurrentName, CurrentArity, CurrentLabel, [Instruction | CurrentOps]}}
    end, {[], none}, Forms),
    Functions = lists:reverse([{function, Name, Arity, Label, lists:reverse(Ops)} | Done]),
    %% OTP 29's beam_asm takes annotations separately; this low-level assembler
    %% bypasses only the compiler's semantic verifier, not libbeam admission.
    {ok, Binary} = beam_asm:module({Mod, Exports, Attributes, #{}, Functions, Labels}, [], [], []),
    ok = file:write_file(Target, Binary).
