%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Generic gc_bif forms, as consumed by the runtime importer (not native BIF calls).
{module,gc_bif_forms}.
{exports,[{bnot_input,1},{list_atom,1},{divide,2}]}.
{attributes,[]}.
{labels,7}.
{function,bnot_input,1,2}.
  {label,1}.
  {func_info,{atom,gc_bif_forms},{atom,bnot_input},1}.
  {label,2}.
  {gc_bif,'bnot',{f,0},1,[{x,0}],{x,0}}.
  return.
{function,list_atom,1,4}.
  {label,3}.
  {func_info,{atom,gc_bif_forms},{atom,list_atom},1}.
  {label,4}.
  {gc_bif,list_to_atom,{f,0},1,[{x,0}],{x,0}}.
  return.
{function,divide,2,6}.
  {label,5}.
  {func_info,{atom,gc_bif_forms},{atom,divide},2}.
  {label,6}.
  {gc_bif,'/',{f,0},2,[{x,0},{x,1}],{x,0}}.
  return.
