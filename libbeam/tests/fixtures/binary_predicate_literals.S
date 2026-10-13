%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,binary_predicate_literals}.
{exports,[{binary,1},{nonbinary,1}]}.
{attributes,[]}.
{labels,7}.
{function,binary,1,2}.
  {label,1}.
  {func_info,{atom,binary_predicate_literals},{atom,binary},1}.
  {label,2}.
  {test,is_binary,{f,3},[{literal,<<1>>}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,3}.
  {move,{atom,no},{x,0}}.
  return.
{function,nonbinary,1,5}.
  {label,4}.
  {func_info,{atom,binary_predicate_literals},{atom,nonbinary},1}.
  {label,5}.
  {test,is_binary,{f,6},[{literal,<<1:7>>}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,6}.
  {move,{atom,no},{x,0}}.
  return.
