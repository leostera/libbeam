{module, bitstring_get_integer_opcode}.
{exports, [{entry,1},{identity,1}]}.
{attributes, []}.
{labels, 23}.

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
    {move,{literal,<<43981:16>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{x,0}}.
    {bs_match,{f,9},{x,0},{commands,[{ensure_at_least,16,1},{integer,1,{literal,[]},16,1,{x,1}}]}}.
    {test,is_eq_exact,{f,9},[{x,1},{integer,43981}]}.
    {allocate,1,1}.
    {move,{atom,nil},{y,0}}.
    {move,{literal,<<4660:16>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{y,0}}.
    {bs_match,{f,12},{y,0},{commands,[{ensure_at_least,16,1},{integer,1,{literal,[]},16,1,{x,1}}]}}.
    {test,is_eq_exact,{f,12},[{x,1},{integer,4660}]}.
    {deallocate,1}.
    {move,{literal,<<305419896:32>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{x,0}}.
    {bs_match,{f,15},{x,0},{commands,[{ensure_at_least,32,1},{integer,1,{literal,[]},32,1,{x,1}}]}}.
    {test,is_eq_exact,{f,15},[{x,1},{integer,305419896}]}.
    {allocate,1,1}.
    {move,{atom,nil},{y,0}}.
    {move,{literal,<<591751049:32>>},{x,0}}.
    {bs_start_match4,{atom,no_fail},1,{x,0},{y,0}}.
    {bs_match,{f,18},{y,0},{commands,[{ensure_at_least,32,1},{integer,1,{literal,[]},32,1,{x,1}}]}}.
    {test,is_eq_exact,{f,18},[{x,1},{integer,591751049}]}.
    {deallocate,1}.
    {move,{literal,<<"ok">>},{x,0}}.
    return.
  {label,3}.
    {move,{atom,unexpected_8_x_result},{x,0}}.
    return.
  {label,6}.
    {deallocate,1}.
    {move,{atom,unexpected_8_y_result},{x,0}}.
    return.
  {label,9}.
    {move,{atom,unexpected_16_x_result},{x,0}}.
    return.
  {label,12}.
    {deallocate,1}.
    {move,{atom,unexpected_16_y_result},{x,0}}.
    return.
  {label,15}.
    {move,{atom,unexpected_32_x_result},{x,0}}.
    return.
  {label,18}.
    {deallocate,1}.
    {move,{atom,unexpected_32_y_result},{x,0}}.
    return.

{function, identity, 1, 22}.
  {label,21}.
    {func_info,{atom,bitstring_get_integer_opcode},{atom,identity},1}.
  {label,22}.
    return.
