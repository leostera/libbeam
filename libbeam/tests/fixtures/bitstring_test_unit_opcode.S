{module, bitstring_test_unit_opcode}.
{exports, [{entry,1},{identity,1}]}.
{attributes, []}.
{labels, 10}.

{function, entry, 1, 2}.
  {label,1}.
    {func_info,{atom,bitstring_test_unit_opcode},{atom,entry},1}.
  {label,2}.
    {move,{literal,<<5:3>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{x,0}}.
    {bs_match,{f,3},{x,0},{commands,[{ensure_at_least,0,3}]}}.
    {allocate,1,1}.
    {move,{atom,nil},{y,0}}.
    {move,{literal,<<5:4>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{y,0}}.
    {bs_match,{f,6},{y,0},{commands,[{ensure_at_least,0,3}]}}.
    {move,{atom,unexpected_unit_success},{x,0}}.
    {deallocate,1}.
    return.
  {label,3}.
    {move,{atom,unexpected_unit_failure},{x,0}}.
    return.
  {label,6}.
    {deallocate,1}.
    {move,{literal,<<"ok">>},{x,0}}.
    return.

{function, identity, 1, 9}.
  {label,8}.
    {func_info,{atom,bitstring_test_unit_opcode},{atom,identity},1}.
  {label,9}.
    return.
