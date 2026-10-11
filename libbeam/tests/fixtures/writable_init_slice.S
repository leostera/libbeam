%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
%% Standard OTP assembler input. Isolates the complete native initializer from
%% comprehension matching/private-append instructions not yet executable.
{module,writable_init_slice}.
{exports,[{init,1},{zero,1},{large,1},{limit,1},{saved,1},{loop,1}]}.
{attributes,[]}.
{labels,13}.

{function,init,1,2}.
  {label,1}.
  {func_info,{atom,writable_init_slice},{atom,init},1}.
  {label,2}.
  {allocate,0,1}.
  bs_init_writable.
  {deallocate,0}.
  return.

{function,zero,1,4}.
  {label,3}.
  {func_info,{atom,writable_init_slice},{atom,zero},1}.
  {label,4}.
  {allocate,0,1}.
  {move,{integer,0},{x,0}}.
  bs_init_writable.
  {deallocate,0}.
  return.

{function,large,1,6}.
  {label,5}.
  {func_info,{atom,writable_init_slice},{atom,large},1}.
  {label,6}.
  {allocate,0,1}.
  {move,{integer,65536},{x,0}}.
  bs_init_writable.
  {deallocate,0}.
  return.

{function,limit,1,8}.
  {label,7}.
  {func_info,{atom,writable_init_slice},{atom,limit},1}.
  {label,8}.
  {allocate,0,1}.
  {move,{integer,67108865},{x,0}}.
  bs_init_writable.
  {deallocate,0}.
  return.

{function,saved,1,10}.
  {label,9}.
  {func_info,{atom,writable_init_slice},{atom,saved},1}.
  {label,10}.
  {allocate,1,1}.
  {move,{x,0},{y,0}}.
  {move,{integer,0},{x,0}}.
  bs_init_writable.
  {move,{y,0},{x,0}}.
  {deallocate,1}.
  return.

{function,loop,1,12}.
  {label,11}.
  {func_info,{atom,writable_init_slice},{atom,loop},1}.
  {label,12}.
  {allocate,0,1}.
  {move,{integer,65536},{x,0}}.
  bs_init_writable.
  {call_last,1,{f,12},0}.
