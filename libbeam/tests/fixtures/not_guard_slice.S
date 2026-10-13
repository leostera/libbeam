%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Exercise an actual guard BIF with moving-stack source/destination locations,
%% rather than a compiler-optimized comparison against the atom false.
{module,not_guard_slice}.
{exports,[{negate,1}]}.
{attributes,[]}.
{labels,4}.
{function,negate,1,2}.
  {label,1}.
  {func_info,{atom,not_guard_slice},{atom,negate},1}.
  {label,2}.
  {allocate,2,1}.
  {init_yregs,{list,[{y,0}]}}.
  {move,{x,0},{y,1}}.
  {bif,'not',{f,3},[{y,1}],{y,0}}.
  {move,{y,0},{x,0}}.
  {deallocate,2}.
  return.
  {label,3}.
  {move,{atom,invalid},{x,0}}.
  {deallocate,2}.
  return.
