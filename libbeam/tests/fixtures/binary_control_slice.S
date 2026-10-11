%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Standard OTP assembly, exposing guard/private/Y boundaries independently of
%% still-unadmitted compiler comprehension matching and interprocedural facts.
{module,binary_control_slice}.
{exports,[{private,1},{guard,1},{y,1},{limit,1},{loop,1},{private_alias,1},{alias,1}]}.
{attributes,[]}.
{labels,16}.

{function,private,1,2}.
  {label,1}.
  {func_info,{atom,binary_control_slice},{atom,private},1}.
  {label,2}.
  {allocate,1,1}.
  {move,{x,0},{y,0}}.
  {move,{integer,0},{x,0}}.
  bs_init_writable.
  {bs_create_bin,{f,0},0,1,8,{x,0},
    {list,[{atom,private_append},1,8,nil,{x,0},{atom,all},
           {atom,binary},2,8,nil,{y,0},{atom,all}]}}.
  {deallocate,1}.
  return.

{function,guard,1,4}.
  {label,3}.
  {func_info,{atom,binary_control_slice},{atom,guard},1}.
  {label,4}.
  {bs_create_bin,{f,5},0,1,8,{x,0},
    {list,[{atom,integer},1,1,nil,{x,0},{integer,8}]}}.
  return.
  {label,5}.
  return.

{function,y,1,7}.
  {label,6}.
  {func_info,{atom,binary_control_slice},{atom,y},1}.
  {label,7}.
  {allocate,2,1}.
  {init_yregs,{list,[{y,0}]}}.
  {move,{x,0},{y,1}}.
  {bs_create_bin,{f,0},0,0,8,{y,0},
    {list,[{atom,binary},1,8,nil,{y,1},{atom,all},
           {atom,integer},2,1,nil,{integer,33},{integer,8}]}}.
  {move,{y,0},{x,0}}.
  {deallocate,2}.
  return.

{function,limit,1,9}.
  {label,8}.
  {func_info,{atom,binary_control_slice},{atom,limit},1}.
  {label,9}.
  {bs_create_bin,{f,0},0,0,8,{x,0},
    {list,[{atom,integer},1,8,nil,{integer,0},{integer,67108865}]}}.
  return.

{function,loop,1,11}.
  {label,10}.
  {func_info,{atom,binary_control_slice},{atom,loop},1}.
  {label,11}.
  {bs_create_bin,{f,0},0,0,8,{x,0},
    {list,[{atom,integer},1,8,nil,{integer,0},{integer,65536}]}}.
  {call_only,1,{f,11}}.

{function,private_alias,1,13}.
  {label,12}.
  {func_info,{atom,binary_control_slice},{atom,private_alias},1}.
  {label,13}.
  {allocate,0,1}.
  {move,{integer,0},{x,0}}.
  bs_init_writable.
  {move,{x,0},{x,1}}.
  {bs_create_bin,{f,0},0,2,8,{x,0},
    {list,[{atom,private_append},1,8,nil,{x,0},{atom,all},
           {atom,integer},2,1,nil,{integer,1},{integer,8}]}}.
  {deallocate,0}.
  return.

{function,alias,1,15}.
  {label,14}.
  {func_info,{atom,binary_control_slice},{atom,alias},1}.
  {label,15}.
  {allocate,0,1}.
  {move,{integer,0},{x,0}}.
  bs_init_writable.
  {bs_create_bin,{f,0},0,1,1,{x,0},
    {list,[{atom,private_append},1,1,nil,{x,0},{atom,all},
           {atom,integer},2,1,nil,{integer,19},{integer,5}]}}.
  {bs_create_bin,{f,0},0,1,1,{x,0},
    {list,[{atom,append},1,1,nil,{x,0},{atom,all},
           {atom,binary},2,1,nil,{x,0},{atom,all}]}}.
  {deallocate,0}.
  return.
