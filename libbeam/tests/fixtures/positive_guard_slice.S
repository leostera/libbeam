%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,positive_guard_slice}.
{exports,[{positive,1}]}.
{attributes,[]}.
{labels,4}.
{function,positive,1,2}.
  {label,1}.
  {func_info,{atom,positive_guard_slice},{atom,positive},1}.
  {label,2}.
  {allocate,2,1}.
  {init_yregs,{list,[{y,0}]}}.
  {move,{x,0},{y,1}}.
  {bif,'+',{f,3},[{y,1}],{y,0}}.
  {move,{y,0},{x,0}}.
  {deallocate,2}.
  return.
  {label,3}.
  {move,{atom,no},{x,0}}.
  {deallocate,2}.
  return.
