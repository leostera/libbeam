%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(hash_slice).
-export([identity/1,copy/1,all/1,base/1,row/1,probe/1,zero/1,negative/1,too_large/1,wrong_type/1]).
identity(B) -> B.
copy(B) -> 7 = probe(7), H = erlang:phash2(B), H = erlang:phash2(B,134217728), B.
probe(N) -> <<Value:8>> = <<N:8>>, Value.
zero(B) -> erlang:phash2(B,0).
negative(B) -> erlang:phash2(B,-1).
too_large(B) -> erlang:phash2(B,4294967297).
wrong_type(B) -> erlang:phash2(B,1.0).
capture(Value) -> fun() -> Value end.
sequence(0,Acc) -> Acc;
sequence(N,Acc) -> sequence(N-1,[N|Acc]).
binary(0,Acc) -> Acc;
binary(N,Acc) -> binary(N-1,<<Acc/binary,(N rem 256)>>).
views(8,_,_,Acc) -> Acc;
views(Offset,8,B,Acc) -> views(Offset+1,0,B,Acc);
views(Offset,Tail,B,Acc) ->
    %% Logical bit values for each offset. Physical unaligned owned views are
    %% checked against these aligned values by core_hash_test's native component.
    View = <<B/binary,3:Tail>>,
    views(Offset,Tail+1,B,[View|Acc]).
all(Input) ->
    N = binary_to_integer(Input),
    rows(views(0,0,binary(N,<<>>),[]),[]).
base(_) ->
    Base = [[],true,false,hello_world,'åäö','λ',0,1,-1,
            134217727,134217728,-134217728,-134217729,4294967295,4294967296,
            -(1 bsl 64),(1 bsl 128)+37,-((1 bsl 128)+37),0.0,-0.0,1.5,-1.5,
            {},{a,1,[2,3]},[1,2,3|tail],[0,255,1,2,3],#{},#{foo=>1,bar=>[2]},
            #{nested=>{#{a=>1},#{b=>2}}},fun erlang:phash2/1,fun identity/1,
            capture(#{captured=>[1,2]}),list_to_tuple(sequence(128,[])),#{1=>1,2=>2,3=>3,4=>4,5=>5,6=>6,7=>7,8=>8,
              9=>9,10=>10,11=>11,12=>12,13=>13,14=>14,15=>15,16=>16,
              17=>17,18=>18,19=>19,20=>20,21=>21,22=>22,23=>23,24=>24,
              25=>25,26=>26,27=>27,28=>28,29=>29,30=>30,31=>31,32=>32,
              33=>33,34=>34,35=>35,36=>36,37=>37,38=>38,39=>39,40=>40,
              41=>41,42=>42,43=>43,44=>44,45=>45,46=>46,47=>47,48=>48,
              49=>49,50=>50,51=>51,52=>52,53=>53,54=>54,55=>55,56=>56,
              57=>57,58=>58,59=>59,60=>60,61=>61,62=>62,63=>63,64=>64},
            sequence(128,[]),[<<"x">>,{a,b},[1|2]]],
    rows(Base,[]).
row(T) ->
    A = integer_to_binary(erlang:phash2(T,4294967296)),
    B = integer_to_binary(erlang:phash2(T)),
    C = integer_to_binary(erlang:phash2(T,1)),
    D = integer_to_binary(erlang:phash2(T,2)),
    E = integer_to_binary(erlang:phash2(T,3)),
    F = integer_to_binary(erlang:phash2(T,16777216)),
    G = integer_to_binary(erlang:phash2(T,4294967295)),
    <<A/binary,":",B/binary,":",C/binary,":",D/binary,":",E/binary,":",F/binary,":",G/binary,"\n">>.
rows([],Acc) -> iolist_to_binary(Acc);
rows([T|Rest],Acc) -> rows(Rest,[row(T)|Acc]).
