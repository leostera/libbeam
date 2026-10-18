{module, debug_line_opcode}.
{exports, [{entry,1},{identity,1}]}.
{attributes, []}.
{labels, 5}.

{function, entry, 1, 2}.
  {label,1}.
    {func_info,{atom,debug_line_opcode},{atom,entry},1}.
  {label,2}.
    {debug_line,{atom,line},[{location,"debug_line_opcode.erl",1}],1,0,#{frame_size=>entry,vars=>[]}}.
    {move,{literal,<<"ok">>},{x,0}}.
    return.

{function, identity, 1, 4}.
  {label,3}.
    {func_info,{atom,debug_line_opcode},{atom,identity},1}.
  {label,4}.
    {move,{x,0},{x,0}}.
    return.
