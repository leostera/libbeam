%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,list_predicate_literals}.
{exports,[{list,1},{nonlist,1}]}.
{attributes,[]}.
{labels,7}.
{function,list,1,2}.
  {label,1}.
  {func_info,{atom,list_predicate_literals},{atom,list},1}.
  {label,2}.
  {test,is_list,{f,3},[{literal,[1|done]}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,3}.
  {move,{atom,no},{x,0}}.
  return.
{function,nonlist,1,5}.
  {label,4}.
  {func_info,{atom,list_predicate_literals},{atom,nonlist},1}.
  {label,5}.
  {test,is_list,{f,6},[{literal,1.0}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,6}.
  {move,{atom,no},{x,0}}.
  return.
