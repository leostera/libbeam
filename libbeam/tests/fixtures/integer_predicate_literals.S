%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,integer_predicate_literals}.
{exports,[{integer,1},{noninteger,1}]}.
{attributes,[]}.
{labels,7}.
{function,integer,1,2}.
  {label,1}.
  {func_info,{atom,integer_predicate_literals},{atom,integer},1}.
  {label,2}.
  {test,is_integer,{f,3},[{integer,18446744073709551616}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,3}.
  {move,{atom,no},{x,0}}.
  return.
{function,noninteger,1,5}.
  {label,4}.
  {func_info,{atom,integer_predicate_literals},{atom,noninteger},1}.
  {label,5}.
  {test,is_integer,{f,6},[{literal,1.0}]}.
  {move,{atom,yes},{x,0}}.
  return.
  {label,6}.
  {move,{atom,no},{x,0}}.
  return.
