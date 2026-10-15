%% SPDX-License-Identifier: Apache-2.0
%% Auxiliary adversarial OTP-compiled assembly (OTP 28), with only the
%% is_nonempty_list guard before get_list y0,x1,x2 deliberately removed.
%% Direct beam_asm assembly is required because OTP's verifier correctly
%% refuses this unguarded form. libbeam's checked yxx path must not dereference nil.
{module, checked_list_slice}.
{exports, [{module_info,0},{module_info,1},{run,1}]}.
{attributes, []}.
{labels, 8}.

{function, run, 1, 2}.
  {label,1}.
    {line,[{location,"checked_list_slice.erl",3}]}.
    {func_info,{atom,checked_list_slice},{atom,run},1}.
  {label,2}.
    {test,is_binary,{f,1},[{x,0}]}.
    {allocate,1,1}.
    {move,{x,0},{y,0}}.
    {line,[{location,"checked_list_slice.erl",4}]}.
    {call_ext,1,{extfunc,erlang,binary_to_list,1}}.
    {line,[{location,"checked_list_slice.erl",5}]}.
    {gc_bif,byte_size,{f,0},1,[{tr,{y,0},{t_bitstring,8,false}}],{x,1}}.
    {move,{x,0},{y,0}}.
    {move,{x,1},{x,0}}.
    {call_ext,1,{extfunc,erlang,integer_to_binary,1}}.
    {get_list,{y,0},{x,1},{x,2}}.
    {line,[{location,"checked_list_slice.erl",7}]}.
    {gc_bif,length,{f,0},3,[{tr,{x,2},{t_list,any,nil}}],{x,2}}.
    {bs_create_bin,{f,0},
                   0,3,8,
                   {x,0},
                   {list,[{atom,integer},
                          1,1,nil,
                          {x,1},
                          {integer,8},
                          {atom,binary},
                          2,8,nil,
                          {x,0},
                          {atom,all},
                          {atom,integer},
                          3,1,nil,
                          {tr,{x,2},{t_integer,{0,288230376151711743}}},
                          {integer,8}]}}.
    {deallocate,1}.
    return.
  {label,3}.
    {line,[{location,"checked_list_slice.erl",6}]}.
    {case_end,nil}.

{function, module_info, 0, 5}.
  {label,4}.
    {line,[]}.
    {func_info,{atom,checked_list_slice},{atom,module_info},0}.
  {label,5}.
    {move,{atom,checked_list_slice},{x,0}}.
    {call_ext_only,1,{extfunc,erlang,get_module_info,1}}.

{function, module_info, 1, 7}.
  {label,6}.
    {line,[]}.
    {func_info,{atom,checked_list_slice},{atom,module_info},1}.
  {label,7}.
    {move,{x,0},{x,1}}.
    {move,{atom,checked_list_slice},{x,0}}.
    {call_ext_only,2,{extfunc,erlang,get_module_info,2}}.
