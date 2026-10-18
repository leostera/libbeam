{module, bitstring_get_integer_opcode}.
{exports, [{entry,1},{identity,1}]}.
{attributes, []}.
{labels, 9}.

{function, entry, 1, 2}.
  {label,1}.
    {func_info,{atom,bitstring_get_integer_opcode},{atom,entry},1}.
  {label,2}.
    {move,{literal,<<123:8>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{x,0}}.
    {bs_match,{f,3},{x,0},{commands,[{ensure_at_least,8,1},{integer,1,{literal,[]},8,1,{x,1}}]}}.
    {test,is_eq_exact,{f,3},[{x,1},{integer,123}]}.
    {allocate,1,1}.
    {move,{atom,nil},{y,0}}.
    {move,{literal,<<124:8>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{y,0}}.
    {bs_match,{f,6},{y,0},{commands,[{ensure_at_least,8,1},{integer,1,{literal,[]},8,1,{x,1}}]}}.
    {test,is_eq_exact,{f,6},[{x,1},{integer,124}]}.
    {deallocate,1}.
    {move,{literal,<<"ok">>},{x,0}}.
    return.
  {label,3}.
    {move,{atom,unexpected_x_result},{x,0}}.
    return.
  {label,6}.
    {deallocate,1}.
    {move,{atom,unexpected_y_result},{x,0}}.
    return.

{function, identity, 1, 8}.
  {label,7}.
    {func_info,{atom,bitstring_get_integer_opcode},{atom,identity},1}.
  {label,8}.
    return.
