%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,atom_predicate_literals}.
{exports,[{atom,1},{nonatom,1}]}.
{attributes,[]}.
{labels,7}.
{function,atom,1,2}.
  {label,1}.
  {func_info,{atom,atom_predicate_literals},{atom,atom},1}.
  {label,2}.
  {test,is_atom,{f,3},[{atom,ordinary_atom}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,3}.
  {move,{atom,no},{x,0}}.
  return.
{function,nonatom,1,5}.
  {label,4}.
  {func_info,{atom,atom_predicate_literals},{atom,nonatom},1}.
  {label,5}.
  {test,is_atom,{f,6},[{literal,1.0}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,6}.
  {move,{atom,no},{x,0}}.
  return.
