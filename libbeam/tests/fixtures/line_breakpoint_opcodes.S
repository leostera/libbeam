{module, line_breakpoint_opcodes}.
{exports, [{entry,1},{identity,1}]}.
{attributes, []}.
{labels, 5}.

{function, entry, 1, 2}.
  {label,1}.
    {func_info,{atom,line_breakpoint_opcodes},{atom,entry},1}.
  {label,2}.
    {line,[{location,"line_breakpoint_opcodes.erl",1}]}.
    {move,{literal,<<"ok">>},{x,0}}.
    return.

{function, identity, 1, 4}.
  {label,3}.
    {func_info,{atom,line_breakpoint_opcodes},{atom,identity},1}.
  {label,4}.
    {line,[]}.
    {move,{x,0},{x,0}}.
    return.
