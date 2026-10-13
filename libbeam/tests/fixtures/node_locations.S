%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
{module,node_locations}.
{exports,[{query,1},{query_y,1}]}.
{attributes,[]}.
{labels,5}.
{function,query,1,2}.
  {label,1}.
  {func_info,{atom,node_locations},{atom,query},1}.
  {label,2}.
  {bif,node,{f,0},[],{x,0}}.
  return.
{function,query_y,1,4}.
  {label,3}.
  {func_info,{atom,node_locations},{atom,query_y},1}.
  {label,4}.
  {allocate,1,1}.
  {bif,node,{f,0},[],{y,0}}.
  {move,{y,0},{x,0}}.
  {deallocate,1}.
  return.
