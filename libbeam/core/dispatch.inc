/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Owned interpreter code adapted from OTP beam_makeops output.
 */
OpCase(move_cr):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  x(0) = I[1];
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_cx):
{
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  xb(I[2]) = I[1];
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_xr):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  x(0) = xb(I[1]);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_xx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = xb(tmp_packed1&BEAM_LOOSE_MASK);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_rx):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb(I[1]) = x(0);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_nx):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb(I[1]) = NIL;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_x1_c):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  x(1) = I[1];
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_x2_c):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  x(2) = I[1];
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_cxx):
{
  Eterm tmp_packed1 = I[2];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm V;
  V = I[1];
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = xb(tmp_packed1&BEAM_LOOSE_MASK);
  xb(tmp_packed1&BEAM_LOOSE_MASK) = V;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_nyx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = NIL;
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = yb(tmp_packed1&BEAM_LOOSE_MASK);
  yb(tmp_packed1&BEAM_LOOSE_MASK) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_xxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = xb(tmp_packed1&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_xyx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = xb(tmp_packed1&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_yry):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = yb(tmp_packed1&BEAM_LOOSE_MASK);
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = x(0);
  x(0) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_yxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = yb(tmp_packed1&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_return_c):
{
  x(0) = I[1];

  SET_I(cp_val(*E));
  *E = NIL;;
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_return' (-no_next)");
}

OpCase(move_return_n):
{
  x(0) = NIL;

  SET_I(cp_val(*E));
  *E = NIL;;
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_return' (-no_next)");
}

OpCase(move_return_x):
{
  x(0) = xb(I[1]);

  SET_I(cp_val(*E));
  *E = NIL;;
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_return' (-no_next)");
}

OpCase(return):
{

  SET_I(cp_val(*E));
  *E = NIL;;
  CHECK_TERM(x(0));
  HEAP_SPACE_VERIFIED(0);

  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'return' (-no_next)");
}

OpCase(put_tuple2_xI):
{
  Eterm tmp_packed1 = I[1];
  Eterm* hp = HTOP;
  Eterm arity = Ib((tmp_packed1>>BEAM_WIDE_SHIFT));

  /*
  * If operands are not packed (in the 32-bit VM),
  * it is not safe to use xb(tmp_packed1&BEAM_WIDE_MASK) directly after I
  * has been updated.
  */
  Eterm* dst_ptr = &(xb(tmp_packed1&BEAM_WIDE_MASK));

  if (!arity || (Uint)(E-HTOP) < arity+1+S_RESERVED || c_p->heap_reserved < arity+1) goto malformed_code;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,arity+lb_work_bulk_units(arity+1))) goto work_failure;
  c_p->heap_reserved -= arity+1;
  *hp++ = make_arityval(arity);
  I = I+2;
  do {
    Eterm term = *I++;
    switch (loader_tag(term)) {
      case LOADER_X_REG:
      *hp++ = x(loader_x_reg_index(term));
      break;
      case LOADER_Y_REG:
      *hp++ = y(loader_y_reg_index(term));
      break;
      default:
      *hp++ = term;
      break;
    }
  } while (--arity != 0);
  *dst_ptr = make_tuple(HTOP);
  HTOP = hp;
  ASSERT(VALID_INSTR(* (Eterm *)I));
  Goto(*I);ASSERT(!"Fell through 'put_tuple2' (-no_next)");
}

OpCase(put_tuple2_yI):
{
  Eterm tmp_packed1 = I[1];
  Eterm* hp = HTOP;
  Eterm arity = Ib((tmp_packed1>>BEAM_WIDE_SHIFT));

  /*
  * If operands are not packed (in the 32-bit VM),
  * it is not safe to use yb(tmp_packed1&BEAM_WIDE_MASK) directly after I
  * has been updated.
  */
  Eterm* dst_ptr = &(yb(tmp_packed1&BEAM_WIDE_MASK));

  if (!arity || (Uint)(E-HTOP) < arity+1+S_RESERVED || c_p->heap_reserved < arity+1) goto malformed_code;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,arity+lb_work_bulk_units(arity+1))) goto work_failure;
  c_p->heap_reserved -= arity+1;
  *hp++ = make_arityval(arity);
  I = I+2;
  do {
    Eterm term = *I++;
    switch (loader_tag(term)) {
      case LOADER_X_REG:
      *hp++ = x(loader_x_reg_index(term));
      break;
      case LOADER_Y_REG:
      *hp++ = y(loader_y_reg_index(term));
      break;
      default:
      *hp++ = term;
      break;
    }
  } while (--arity != 0);
  *dst_ptr = make_tuple(HTOP);
  HTOP = hp;
  ASSERT(VALID_INSTR(* (Eterm *)I));
  Goto(*I);ASSERT(!"Fell through 'put_tuple2' (-no_next)");
}

OpCase(test_heap_It):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  do {
    Uint need = Ib(tmp_packed1&BEAM_WIDE_MASK) + 0;
    if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
      do {
        //
        // Since a garbage collection is expensive anyway, we can afford
        // to save the instruction counter so that the correct function will
        // be pointed in the crash dump if the garbage collection fails
        // because of insufficient memory.
        //
        SWAPOUT;
        c_p->i = I;
      } while (0);

      if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>BEAM_WIDE_SHIFT)))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


      SWAPIN;

    }
    HEAP_SPACE_VERIFIED(Ib(tmp_packed1&BEAM_WIDE_MASK));
  } while (0);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_call_ext_only_e):
{
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(I[1]);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_call_ext_only' (-no_next)");
}

OpCase(i_move_call_ext_only_ec):
{
  Eterm call_dest = I[1];
  Eterm src = I[2];
  x(0) = src;
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(call_dest);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_move_call_ext_only' (-no_next)");
}

OpCase(jump_f):
{
  ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
  I += (Sint)I[1];;
  Goto(*I);;ASSERT(!"Fell through 'jump' (-no_next)");
}

OpCase(badmatch_x):
{
  c_p->fvalue = xb(I[1]);
  c_p->freason = BADMATCH;
  goto find_func_info;
  ASSERT(!"Fell through 'badmatch' (-no_next)");
}

OpCase(case_end_x):
{
  c_p->fvalue = xb(I[1]);
  c_p->freason = EXC_CASE_CLAUSE;
  goto find_func_info;
  ASSERT(!"Fell through 'case_end' (-no_next)");
}

OpCase(i_allocate_zero_tt):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* ptr;
  int i = tb(tmp_packed1&BEAM_LOOSE_MASK);
  do {
    unsigned needed = i + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = 0 + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>BEAM_LOOSE_SHIFT)))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(0);
    } while (0);
    E -= needed;
    *E = NIL;
  } while (0);
  for (ptr = E + i; ptr > E; ptr--) {
    make_blank(*ptr);
  }
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_allocate_heap_zero_tIt):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* ptr;
  int i = tb(tmp_packed1&BEAM_WIDE_MASK);
  do {
    unsigned needed = i + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = Ib((tmp_packed1>>BEAM_WIDE_SHIFT)) + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, I[2])) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(Ib((tmp_packed1>>BEAM_WIDE_SHIFT)));
    } while (0);
    E -= needed;
    *E = NIL;
  } while (0);
  for (ptr = E + i; ptr > E; ptr--) {
    make_blank(*ptr);
  }
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(allocate_tt):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  do {
    unsigned needed = tb(tmp_packed1&BEAM_LOOSE_MASK) + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = 0 + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>BEAM_LOOSE_SHIFT)))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(0);
    } while (0);
    E -= needed;
    *E = NIL;
    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;
  } while (0);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(allocate_heap_tIt):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  do {
    unsigned needed = tb(tmp_packed1&BEAM_WIDE_MASK) + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = Ib((tmp_packed1>>BEAM_WIDE_SHIFT)) + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, I[2])) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(Ib((tmp_packed1>>BEAM_WIDE_SHIFT)));
    } while (0);
    E -= needed;
    *E = NIL;
    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_call_ext_e):
{
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(I[1]);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_call_ext' (-no_next)");
}

OpCase(i_move_call_ext_ce):
{
  Eterm call_dest = I[2];
  Eterm src = I[1];
  ASSERT(VALID_INSTR(*(I+3)));
  *E = (BeamInstr) (I+3);;
  x(0) = src;
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(call_dest);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_move_call_ext' (-no_next)");
}

OpCase(deallocate_Q):
{
  E = ADD_BYTE_OFFSET(E, I[1]);
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_eq_exact_immed_frc):
{
  if (x(0) != I[2]) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_eq_exact_immed_fxc):
{
  Eterm tmp_packed1 = I[1];
  if (xb((tmp_packed1>>BEAM_WIDE_SHIFT)) != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(move_jump_fcr):
{
  Eterm lbl = I[1];
  Eterm src = I[2];
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)lbl)));
  I += (Sint)lbl;;
  Goto(*I);;ASSERT(!"Fell through 'move_jump' (-no_next)");
}

OpCase(move_jump_fcx):
{
  Eterm tmp_packed1 = I[1];
  Eterm lbl = fb(tmp_packed1&BEAM_WIDE_MASK);
  Eterm src = I[2];
  xb((tmp_packed1>>BEAM_WIDE_SHIFT)) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)lbl)));
  I += (Sint)lbl;;
  Goto(*I);;ASSERT(!"Fell through 'move_jump' (-no_next)");
}

OpCase(move_xy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = xb(tmp_packed1&BEAM_LOOSE_MASK);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_yr):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  x(0) = yb(I[1]);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_yx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = yb(tmp_packed1&BEAM_LOOSE_MASK);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_yy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = yb(tmp_packed1&BEAM_LOOSE_MASK);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_ry):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  yb(I[1]) = x(0);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_cy):
{
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  yb(I[2]) = I[1];
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(swap_xx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V = xb(tmp_packed1&BEAM_LOOSE_MASK);
  xb(tmp_packed1&BEAM_LOOSE_MASK) = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(swap_yx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V = yb(tmp_packed1&BEAM_LOOSE_MASK);
  yb(tmp_packed1&BEAM_LOOSE_MASK) = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(swap_yy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V = yb(tmp_packed1&BEAM_LOOSE_MASK);
  yb(tmp_packed1&BEAM_LOOSE_MASK) = yb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(swap2_xxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = xb(tmp_packed1&BEAM_TIGHT_MASK);
  xb(tmp_packed1&BEAM_TIGHT_MASK) = xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT)));
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_call_f):
{
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
  I += (Sint)I[1];;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_call' (-no_next)");
}

OpCase(i_call_last_fQ):
{
  Eterm tmp_packed1 = I[1];
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_WIDE_SHIFT)));;
  ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
  I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_call_last' (-no_next)");
}

OpCase(i_call_only_f):
{
  ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
  I += (Sint)I[1];;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_call_only' (-no_next)");
}

OpCase(i_times_jssd):
{
  Eterm tmp_packed1 = I[1];
  Eterm targ1;
  Eterm targ2;
  Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  GetSource(I[2], targ1);
  GetSource(I[3], targ2);
  {
    Eterm op1 = targ1;
    Eterm op2 = targ2;
#ifdef HAVE_OVERFLOW_CHECK_BUILTINS
    if (ERTS_LIKELY(is_both_small(op1, op2))) {
      /* See plus.execute */
      Sint lhs_untagged, rhs_actual, res;

      lhs_untagged = op1 & ~_TAG_IMMED1_MASK;
      rhs_actual = signed_val(op2);

      if (ERTS_LIKELY(!__builtin_mul_overflow(lhs_untagged, rhs_actual, &res))) {
        ASSERT(!(res & _TAG_IMMED1_MASK));
        *dst_ptr = res | _TAG_IMMED1_SMALL;
        SET_I((BeamInstr *) I+4);
        Goto(*I);;
      }
    }
#endif
    do {
      Eterm result;



      SWAPOUT; result = lb_mixed_times(c_p, op1, op2); SWAPIN;
      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;
      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;
      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;




      if (ERTS_LIKELY(is_value(result))) {
        *dst_ptr = result;
        SET_I((BeamInstr *) I+4);
        Goto(*I);;
      }
      if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
        ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
        I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
        Goto(*I);;
      }
      reg[0] = op1;
      reg[1] = op2;
      SWAPOUT;
      goto find_func_info;;
    } while (0);ASSERT(!"Fell through 'i_times' (-no_next)");
  }
}

OpCase(i_int_div_jssd):
{
  Eterm tmp_packed1 = I[1];
  Eterm targ1;
  Eterm targ2;
  Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  GetSource(I[2], targ1);
  GetSource(I[3], targ2);
  {
    Eterm op1 = targ1;
    Eterm op2 = targ2;

    if (ERTS_UNLIKELY(op2 == SMALL_ZERO)) {
      c_p->freason = BADARITH;
      if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
        ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
        I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
        Goto(*I);;
      }
      reg[0] = op1;
      reg[1] = op2;
      SWAPOUT;
      goto find_func_info;;
    } else if (ERTS_LIKELY(is_both_small(op1, op2))) {
      Sint ires = signed_val(op1) / signed_val(op2);

      /* We could skip this check if it weren't for the fact that dividing
      * MIN_SMALL by -1 causes an overflow, and we have nothing to gain from
      * fixed-point optimizing this instruction since there's no
      * __builtin_div_overflow. */
      if (ERTS_LIKELY(IS_SSMALL(ires))) {
        *dst_ptr = make_small(ires);
        SET_I((BeamInstr *) I+4);
        Goto(*I);;
      }
    }

    {
      do {
        int succeeded;
        Eterm q, r;



        SWAPOUT; succeeded = lb_int_div_rem(c_p, op1, op2, &q, &r); SWAPIN;
        dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
        if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
        if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
        if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
        if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;




        if (ERTS_LIKELY(succeeded)) {
          *dst_ptr = q;
          SET_I((BeamInstr *) I+4);
          Goto(*I);;
        }
        if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
          ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
          I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
          Goto(*I);;
        }
        reg[0] = op1;
        reg[1] = op2;
        SWAPOUT;
        goto find_func_info;;
      } while (0);
    }ASSERT(!"Fell through 'i_int_div' (-no_next)");
  }
}

OpCase(move_call_cf):
{
  Eterm call_dest = I[2];
  Eterm src = I[1];
  ASSERT(VALID_INSTR(*(I+3)));
  *E = (BeamInstr) (I+3);;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call' (-no_next)");
}

OpCase(move_call_xf):
{
  Eterm tmp_packed1 = I[1];
  Eterm call_dest = fb((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm src = xb(tmp_packed1&BEAM_WIDE_MASK);
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call' (-no_next)");
}

OpCase(move_call_yf):
{
  Eterm tmp_packed1 = I[1];
  Eterm call_dest = fb((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm src = yb(tmp_packed1&BEAM_WIDE_MASK);
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call' (-no_next)");
}

OpCase(move_call_last_cfQ):
{
  Eterm tmp_packed1 = I[2];
  Eterm call_dest = fb(tmp_packed1&BEAM_WIDE_MASK);
  Eterm src = I[1];
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_WIDE_SHIFT)));;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call_last' (-no_next)");
}

OpCase(move_call_last_xfQ):
{
  Eterm tmp_packed1 = I[1];
  Eterm call_dest = I[2];
  Eterm src = xb(tmp_packed1&BEAM_LOOSE_MASK);
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_LOOSE_SHIFT)));;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call_last' (-no_next)");
}

OpCase(move_call_last_yfQ):
{
  Eterm tmp_packed1 = I[1];
  Eterm call_dest = I[2];
  Eterm src = yb(tmp_packed1&BEAM_LOOSE_MASK);
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_LOOSE_SHIFT)));;
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call_last' (-no_next)");
}

OpCase(move_call_only_cf):
{
  Eterm call_dest = I[2];
  Eterm src = I[1];
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call_only' (-no_next)");
}

OpCase(move_call_only_xf):
{
  Eterm tmp_packed1 = I[1];
  Eterm call_dest = fb((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm src = xb(tmp_packed1&BEAM_WIDE_MASK);
  x(0) = src;
  ASSERT(VALID_INSTR(*(I + (Sint)call_dest)));
  I += (Sint)call_dest;;


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'move_call_only' (-no_next)");
}

OpCase(move_deallocate_return_cQ):
{

  /*
  * Explicitly do reads first to mitigate the impact of read
  * dependencies.
  */

  Uint bytes_to_pop = I[2];
  Eterm src = I[1];
  E = ADD_BYTE_OFFSET(E, bytes_to_pop);
  x(0) = src;

  SET_I(cp_val(*E));
  *E = NIL;;
  CHECK_TERM(x(0));
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_deallocate_return' (-no_next)");
}

OpCase(move_deallocate_return_nQ):
{

  /*
  * Explicitly do reads first to mitigate the impact of read
  * dependencies.
  */

  Uint bytes_to_pop = I[1];
  Eterm src = NIL;
  E = ADD_BYTE_OFFSET(E, bytes_to_pop);
  x(0) = src;

  SET_I(cp_val(*E));
  *E = NIL;;
  CHECK_TERM(x(0));
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_deallocate_return' (-no_next)");
}

OpCase(move_deallocate_return_xQ):
{
  Eterm tmp_packed1 = I[1];

  /*
  * Explicitly do reads first to mitigate the impact of read
  * dependencies.
  */

  Uint bytes_to_pop = Qb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  Eterm src = xb(tmp_packed1&BEAM_LOOSE_MASK);
  E = ADD_BYTE_OFFSET(E, bytes_to_pop);
  x(0) = src;

  SET_I(cp_val(*E));
  *E = NIL;;
  CHECK_TERM(x(0));
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_deallocate_return' (-no_next)");
}

OpCase(move_deallocate_return_yQ):
{
  Eterm tmp_packed1 = I[1];

  /*
  * Explicitly do reads first to mitigate the impact of read
  * dependencies.
  */

  Uint bytes_to_pop = Qb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  Eterm src = yb(tmp_packed1&BEAM_LOOSE_MASK);
  E = ADD_BYTE_OFFSET(E, bytes_to_pop);
  x(0) = src;

  SET_I(cp_val(*E));
  *E = NIL;;
  CHECK_TERM(x(0));
  if (FCALLS > 0 || FCALLS > neg_o_reds) {
    FCALLS--;
    Goto(*I);
  } else {
    c_p->current = NULL;
    c_p->arity = 1;
    goto context_switch3;
  };ASSERT(!"Fell through 'move_deallocate_return' (-no_next)");
}

OpCase(i_call_ext_last_eQ):
{
  E = ADD_BYTE_OFFSET(E, I[2]);;
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(I[1]);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_call_ext_last' (-no_next)");
}

OpCase(i_move_call_ext_last_eQc):
{
  Eterm call_dest = I[1];
  Eterm src = I[3];
  E = ADD_BYTE_OFFSET(E, I[2]);;
  x(0) = src;
  do {
    BeamInstr dis_next;
    Export *ep;

    ep = (Export*)(call_dest);


    SET_I(ep->dispatch.addresses[0]);
    CHECK_ARGS(I);
    dis_next = *I;

    if (ERTS_UNLIKELY(FCALLS <= 0)) {
      goto context_switch;
    }

    FCALLS--;
    Goto(dis_next);
  } while (0);ASSERT(!"Fell through 'i_move_call_ext_last' (-no_next)");
}

OpCase(i_init_y):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  make_blank(yb(I[1]));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_init2_yy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  make_blank(yb(tmp_packed1&BEAM_LOOSE_MASK));
  make_blank(yb((tmp_packed1>>BEAM_LOOSE_SHIFT)));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_init3_yyy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  make_blank(yb(tmp_packed1&BEAM_TIGHT_MASK));
  make_blank(yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK));
  make_blank(yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_init_seq3_y):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* dst = &yb(I[1]);
  make_blank(dst[0]);
  make_blank(dst[1]);
  make_blank(dst[2]);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_init_seq4_y):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* dst = &yb(I[1]);
  make_blank(dst[0]);
  make_blank(dst[1]);
  make_blank(dst[2]);
  make_blank(dst[3]);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_init_seq5_y):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* dst = &yb(I[1]);
  make_blank(dst[0]);
  make_blank(dst[1]);
  make_blank(dst[2]);
  make_blank(dst[3]);
  make_blank(dst[4]);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_is_eq_exact_immed_fyc):
{
  Eterm tmp_packed1 = I[1];
  if (yb((tmp_packed1>>BEAM_WIDE_SHIFT)) != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_eq_exact_literal_fxc):
{
  Eterm tmp_packed1 = I[1];
  Eterm src = xb((tmp_packed1>>BEAM_WIDE_SHIFT));
  int lb_equal=0;
  if (!is_immed(src) && lb_term_equal(c_p->entry_module->space->domain, src, I[2], &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (is_immed(src) || !lb_equal) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_eq_exact_literal_fyc):
{
  Eterm tmp_packed1 = I[1];
  Eterm src = yb((tmp_packed1>>BEAM_WIDE_SHIFT));
  int lb_equal=0;
  if (!is_immed(src) && lb_term_equal(c_p->entry_module->space->domain, src, I[2], &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (is_immed(src) || !lb_equal) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_ne_exact_immed_fxc):
{
  Eterm tmp_packed1 = I[1];
  if (xb((tmp_packed1>>BEAM_WIDE_SHIFT)) == I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_ne_exact_immed_fyc):
{
  Eterm tmp_packed1 = I[1];
  if (yb((tmp_packed1>>BEAM_WIDE_SHIFT)) == I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_ne_exact_literal_fxc):
{
  Eterm tmp_packed1 = I[1];
  Eterm src = xb((tmp_packed1>>BEAM_WIDE_SHIFT));
  int lb_equal=0;
  if (!is_immed(src) && lb_term_equal(c_p->entry_module->space->domain, src, I[2], &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (!is_immed(src) && lb_equal) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_is_ne_exact_literal_fyc):
{
  Eterm tmp_packed1 = I[1];
  Eterm src = yb((tmp_packed1>>BEAM_WIDE_SHIFT));
  int lb_equal=0;
  if (!is_immed(src) && lb_term_equal(c_p->entry_module->space->domain, src, I[2], &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (!is_immed(src) && lb_equal) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_eq_exact_fxx):
{
  Eterm tmp_packed1 = I[2];
  int lb_equal=0;
  if (lb_term_equal(c_p->entry_module->space->domain, xb(tmp_packed1&BEAM_LOOSE_MASK), xb((tmp_packed1>>BEAM_LOOSE_SHIFT)), &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (!lb_equal) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_eq_exact_fxy):
{
  Eterm tmp_packed1 = I[2];
  int lb_equal=0;
  if (lb_term_equal(c_p->entry_module->space->domain, xb(tmp_packed1&BEAM_LOOSE_MASK), yb((tmp_packed1>>BEAM_LOOSE_SHIFT)), &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (!lb_equal) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_eq_exact_fyy):
{
  Eterm tmp_packed1 = I[2];
  int lb_equal=0;
  if (lb_term_equal(c_p->entry_module->space->domain, yb(tmp_packed1&BEAM_LOOSE_MASK), yb((tmp_packed1>>BEAM_LOOSE_SHIFT)), &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (!lb_equal) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_ne_exact_fSS):
{
  Eterm tmp_packed1 = I[2];
  int lb_equal=0;
  if (lb_term_equal(c_p->entry_module->space->domain, Sb(tmp_packed1&BEAM_LOOSE_MASK), Sb((tmp_packed1>>BEAM_LOOSE_SHIFT)), &lb_equal)!=LB_ALLOC_OK) goto memory_failure;
  if (lb_equal) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nil_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_nil(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nil_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_nil(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_sd):
{
  Eterm targ1;
  Eterm dst = I[2];
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  GetSource(I[1], targ1);
  {
    Eterm args[1]={targ1};
    Eterm result=lb_bif_get_1(c_p,args,I);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(is_non_value(result)) goto malformed_code;
    *dst_ptr=result;
    I += 3;
    ASSERT(VALID_INSTR(next_pf));
    if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);
  }
}

OpCase(i_get_hash_cWd):
{
  Eterm dst = I[3];
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  BeamInstr next_pf = BeamCodeAddr(I[4]);
  ASSERT(is_small(I[1]) || is_atom(I[1]));
    ASSERT(I[2]==(is_small(I[1])?I[1]>>_TAG_IMMED1_SIZE:atom_val(I[1])));
Eterm args[1]={I[1]};
    Eterm result=lb_bif_get_1(c_p,args,I);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(is_non_value(result)) goto malformed_code;
    *dst_ptr=result;
  I += 4;
  ASSERT(VALID_INSTR(next_pf));
  if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);
}

OpCase(i_apply):
{
  const BeamInstr *next;
  do {
    const Export *ep;

    SWAPOUT; c_p->heap_reserved=0; ep=lb_apply(c_p); SWAPIN;
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

    if (ERTS_UNLIKELY(ep == NULL)) {
      do {
      c_p->current=&c_p->entry_module->space->apply_export.info.mfa;
        goto find_func_info;
      } while (0);
    }

    next = ep->dispatch.addresses[0];
  } while (0);

  ASSERT(VALID_INSTR(*(I+1)));
  *E = (BeamInstr) (I+1);;
  SET_I((BeamInstr *) next);


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_apply' (-no_next)");
}

OpCase(i_apply_last_Q):
{
  const BeamInstr *next;
  do {
    const Export *ep;

    SWAPOUT; c_p->heap_reserved=0; ep=lb_apply(c_p); SWAPIN;
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

    if (ERTS_UNLIKELY(ep == NULL)) {
      do {
      c_p->current=&c_p->entry_module->space->apply_export.info.mfa;
        goto find_func_info;
      } while (0);
    }

    next = ep->dispatch.addresses[0];
  } while (0);
  E = ADD_BYTE_OFFSET(E, I[1]);;
  SET_I((BeamInstr *) next);


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_apply_last' (-no_next)");
}

OpCase(i_apply_only):
{
  const BeamInstr *next;
  do {
    const Export *ep;

    SWAPOUT; c_p->heap_reserved=0; ep=lb_apply(c_p); SWAPIN;
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

    if (ERTS_UNLIKELY(ep == NULL)) {
      do {
      c_p->current=&c_p->entry_module->space->apply_export.info.mfa;
        goto find_func_info;
      } while (0);
    }

    next = ep->dispatch.addresses[0];
  } while (0);
  SET_I((BeamInstr *) next);


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'i_apply_only' (-no_next)");
}

OpCase(apply_t):
{
  const BeamInstr *next;
  do {
    const Export *ep;

    SWAPOUT; c_p->heap_reserved=0; ep=lb_fixed_apply(c_p,I[1]); SWAPIN;
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

    if (ERTS_UNLIKELY(ep == NULL)) {
      do {
      c_p->current=&c_p->entry_module->space->apply_export.info.mfa;
        goto find_func_info;
      } while (0);
    }

    next = ep->dispatch.addresses[0];
  } while (0);
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  SET_I((BeamInstr *) next);


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'apply' (-no_next)");
}

OpCase(apply_last_tQ):
{
  Eterm tmp_packed1 = I[1];
  const BeamInstr *next;
  do {
    const Export *ep;

    SWAPOUT; c_p->heap_reserved=0; ep=lb_fixed_apply(c_p,tb(tmp_packed1&BEAM_LOOSE_MASK)); SWAPIN;
    FCALLS-=(Sint)c_p->last_native_cost;
    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;
    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

    if (ERTS_UNLIKELY(ep == NULL)) {
      do {
      c_p->current=&c_p->entry_module->space->apply_export.info.mfa;
        goto find_func_info;
      } while (0);
    }

    next = ep->dispatch.addresses[0];
  } while (0);
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_LOOSE_SHIFT)));;
  SET_I((BeamInstr *) next);


  do {
    BeamInstr dis_next;

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);;ASSERT(!"Fell through 'apply_last' (-no_next)");
}

OpCase(is_binary_fx):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_binary(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_binary_fy):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_binary(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_bitstring_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_bitstring(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_bitstring_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_bitstring(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_boolean_fx):
{
  Eterm tmp_packed1 = I[1];
  if ((xb((tmp_packed1>>BEAM_WIDE_SHIFT))) != am_true && (xb((tmp_packed1>>BEAM_WIDE_SHIFT))) != am_false) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_boolean_fy):
{
  Eterm tmp_packed1 = I[1];
  if ((yb((tmp_packed1>>BEAM_WIDE_SHIFT))) != am_true && (yb((tmp_packed1>>BEAM_WIDE_SHIFT))) != am_false) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_float_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_float(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_float_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_float(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_number_fx):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_integer(xb((tmp_packed1>>BEAM_WIDE_SHIFT))) && is_not_float(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_number_fy):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_integer(yb((tmp_packed1>>BEAM_WIDE_SHIFT))) && is_not_float(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_port_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_port(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_port_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_port(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_pid_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_pid(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_reference_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_ref(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_list_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_list(xb((tmp_packed1>>BEAM_WIDE_SHIFT))) && is_not_nil(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_list_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_list(yb((tmp_packed1>>BEAM_WIDE_SHIFT))) && is_not_nil(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_integer_fx):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_integer(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_integer_fy):
{
  Eterm tmp_packed1 = I[1];
  if (!lb_is_integer(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_integer_allocate_fxtt):
{
  Eterm tmp_packed1 = I[2];
  if (!lb_is_integer(xb(tmp_packed1&BEAM_TIGHT_MASK))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    unsigned needed = tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = 0 + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(0);
    } while (0);
    E -= needed;
    *E = NIL;
    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_fr):
{
  if (is_not_tuple(x(0))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_tuple(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_tuple(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_of_arity_frA):
{
  Eterm term = x(0);
  if (!(BEAM_IS_TUPLE(term) && *tuple_val(term) == I[2])) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_of_arity_fxA):
{
  Eterm tmp_packed1 = I[1];
  Eterm term = xb((tmp_packed1>>BEAM_WIDE_SHIFT));
  if (!(BEAM_IS_TUPLE(term) && *tuple_val(term) == I[2])) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tuple_of_arity_fyA):
{
  Eterm tmp_packed1 = I[1];
  Eterm term = yb((tmp_packed1>>BEAM_WIDE_SHIFT));
  if (!(BEAM_IS_TUPLE(term) && *tuple_val(term) == I[2])) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tagged_tuple_frAa):
{
  Eterm term = x(0);
  if (!(BEAM_IS_TUPLE(term) &&
  (tuple_val(term))[0] == I[2] &&
  (tuple_val(term))[1] == I[3])) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 4;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tagged_tuple_fxAa):
{
  Eterm tmp_packed1 = I[1];
  Eterm term = xb((tmp_packed1>>BEAM_WIDE_SHIFT));
  if (!(BEAM_IS_TUPLE(term) &&
  (tuple_val(term))[0] == I[2] &&
  (tuple_val(term))[1] == I[3])) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 4;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tagged_tuple_fyAa):
{
  Eterm tmp_packed1 = I[1];
  Eterm term = yb((tmp_packed1>>BEAM_WIDE_SHIFT));
  if (!(BEAM_IS_TUPLE(term) &&
  (tuple_val(term))[0] == I[2] &&
  (tuple_val(term))[1] == I[3])) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 4;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tagged_tuple_ff_ffrAa):
{
  Eterm term = x(0);
  if (is_not_tuple(term)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  } else if (ERTS_UNLIKELY((tuple_val(term))[0] != I[3] ||
  (tuple_val(term))[1] != I[4])) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[2])));
    I += (Sint)I[2];;
    Goto(*I);;
  }
  I += 5;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_tagged_tuple_ff_ffxAa):
{
  Eterm tmp_packed1 = I[1];
  Eterm term = xb((tmp_packed1>>BEAM_WIDE_SHIFT));
  if (is_not_tuple(term)) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  } else if (ERTS_UNLIKELY((tuple_val(term))[0] != I[3] ||
  (tuple_val(term))[1] != I[4])) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[2])));
    I += (Sint)I[2];;
    Goto(*I);;
  }
  I += 5;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(test_arity_fxA):
{
  Eterm tmp_packed1 = I[1];
  if (*tuple_val(xb((tmp_packed1>>BEAM_WIDE_SHIFT))) != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(test_arity_fyA):
{
  Eterm tmp_packed1 = I[1];
  if (*tuple_val(yb((tmp_packed1>>BEAM_WIDE_SHIFT))) != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(test_arity_get_tuple_element_fxAPx):
{
  Eterm tmp_packed1 = I[2];
  Eterm* ptr = tuple_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  Eterm* src;
  if (*ptr != I[3]) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  src = ADD_BYTE_OFFSET(ptr, I[4]);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = *src;
  I += 5;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_tuple_element_xPx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = *src;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element_xPy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = *src;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element_yPx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src = ADD_BYTE_OFFSET(tuple_val(yb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = *src;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element_yPy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src = ADD_BYTE_OFFSET(tuple_val(yb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = *src;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element2_xPx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src;
  Eterm* dst;
  Eterm E1, E2;
  src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  dst = &(xb((tmp_packed1>>BEAM_LOOSE_SHIFT)));
  E1 = src[0];
  E2 = src[1];
  dst[0] = E1;
  dst[1] = E2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element3_xPx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src;
  Eterm* dst;
  Eterm E1, E2, E3;
  src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_LOOSE_MASK)), I[2]);
  dst = &(xb((tmp_packed1>>BEAM_LOOSE_SHIFT)));
  E1 = src[0];
  E2 = src[1];
  E3 = src[2];
  dst[0] = E1;
  dst[1] = E2;
  dst[2] = E3;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element2_dst_xPxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src;
  Eterm E1, E2;
  src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_TIGHT_MASK)), I[2]);
  E1 = src[0];
  E2 = src[1];
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = E1;
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = E2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_get_tuple_element2_dst_xPyy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  Eterm* src;
  Eterm E1, E2;
  src = ADD_BYTE_OFFSET(tuple_val(xb(tmp_packed1&BEAM_TIGHT_MASK)), I[2]);
  E1 = src[0];
  E2 = src[1];
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = E1;
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = E2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(is_nonempty_list_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_list(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_list(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_allocate_frtt):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(x(0))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    unsigned needed = tb(tmp_packed1&BEAM_LOOSE_MASK) + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = 0 + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>BEAM_LOOSE_SHIFT)))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(0);
    } while (0);
    E -= needed;
    *E = NIL;
    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_allocate_fxtt):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(xb(tmp_packed1&BEAM_TIGHT_MASK))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    unsigned needed = tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) + 1;
    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;
    do {
      Uint need = 0 + needed;
      if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
        do {
          //
          // Since a garbage collection is expensive anyway, we can afford
          // to save the instruction counter so that the correct function will
          // be pointed in the crash dump if the garbage collection fails
          // because of insufficient memory.
          //
          SWAPOUT;
          c_p->i = I;
        } while (0);

        if (!lb_process_collect_live(c_p, need, tb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))))) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


        SWAPIN;

      }
      HEAP_SPACE_VERIFIED(0);
    } while (0);
    E -= needed;
    *E = NIL;
    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_get_hd_fxx):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(xb(tmp_packed1&BEAM_LOOSE_MASK))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
    xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CAR(tmp_ptr);
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_get_tl_fxx):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(xb(tmp_packed1&BEAM_LOOSE_MASK))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
    xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CDR(tmp_ptr);
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_get_list_frxx):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(x(0))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    Eterm* tmp_ptr = list_val(x(0));
    Eterm hd, tl;
    hd = CAR(tmp_ptr);
    tl = CDR(tmp_ptr);
    xb(tmp_packed1&BEAM_LOOSE_MASK) = hd;
    xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = tl;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_nonempty_list_get_list_fxxx):
{
  Eterm tmp_packed1 = I[2];
  if (is_not_list(xb(tmp_packed1&BEAM_TIGHT_MASK))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  };
  do {
    Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_TIGHT_MASK));
    Eterm hd, tl;
    hd = CAR(tmp_ptr);
    tl = CDR(tmp_ptr);
    xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
    xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  } while (0);
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(get_hd_xx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CAR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_hd_xy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CAR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_hd_yx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CAR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_hd_yy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CAR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_tl_xx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CDR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_tl_xy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CDR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_tl_yx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CDR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_tl_yy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = CDR(tmp_ptr);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_rry):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(x(0));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  x(0) = hd;
  yb(I[1]) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_rxr):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(x(0));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb(I[1]) = hd;
  x(0) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_rxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(x(0));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb(tmp_packed1&BEAM_LOOSE_MASK) = hd;
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_rxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(x(0));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb(tmp_packed1&BEAM_LOOSE_MASK) = hd;
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_ryr):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(x(0));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  yb(I[1]) = hd;
  x(0) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_xrx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  x(0) = hd;
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_xxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_xxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_xyx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_xyy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(xb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_yxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_yxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_yyx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(get_list_yyy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm* tmp_ptr = list_val(yb(tmp_packed1&BEAM_TIGHT_MASK));
  Eterm hd, tl;
  hd = CAR(tmp_ptr);
  tl = CDR(tmp_ptr);
  yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = hd;
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = tl;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_cxx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[2];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  HTOP[0] = I[1];
  HTOP[1] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_cyx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[2];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  HTOP[0] = I[1];
  HTOP[1] = yb(tmp_packed1&BEAM_LOOSE_MASK);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_rnr):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  BeamInstr next_pf = BeamCodeAddr(I[1]);
  HTOP[0] = x(0);
  HTOP[1] = NIL;
  x(0) = make_list(HTOP);
  HTOP += 2;
  I += 1;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_rnx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = x(0);
  HTOP[1] = NIL;
  xb(I[1]) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_rxr):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = x(0);
  HTOP[1] = xb(I[1]);
  x(0) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_rxx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = x(0);
  HTOP[1] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xcx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  HTOP[0] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = I[2];
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xnx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = NIL;
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xny):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = NIL;
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xxr):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  x(0) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xxx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_TIGHT_MASK);
  HTOP[1] = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_xyx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_TIGHT_MASK);
  HTOP[1] = yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_ycx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  HTOP[0] = yb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = I[2];
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_ynx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = yb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = NIL;
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_yny):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = yb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = NIL;
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_yxx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = yb(tmp_packed1&BEAM_TIGHT_MASK);
  HTOP[1] = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_yyx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = yb(tmp_packed1&BEAM_TIGHT_MASK);
  HTOP[1] = yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(put_list_ssd):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm targ1;
  Eterm targ2;
  Eterm dst = I[3];
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  BeamInstr next_pf = BeamCodeAddr(I[4]);
  GetSource(I[1], targ1);
  GetSource(I[2], targ2);
  {
    HTOP[0] = targ1;
    HTOP[1] = targ2;
    *dst_ptr = make_list(HTOP);
    HTOP += 2;
    I += 4;
    ASSERT(VALID_INSTR(next_pf));
    GotoPF(next_pf);
  }
}

OpCase(update_list_cx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  BeamInstr next_pf = BeamCodeAddr(I[3]);
  HTOP[0] = I[1];
  HTOP[1] = xb(I[2]);
  xb(I[2]) = make_list(HTOP);
  HTOP += 2;
  I += 3;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(update_list_xx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = xb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(update_list_yx):
{
  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;
  c_p->heap_reserved-=2;
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  HTOP[0] = yb(tmp_packed1&BEAM_LOOSE_MASK);
  HTOP[1] = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = make_list(HTOP);
  HTOP += 2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_trim_t):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  E += I[1];

  /*
  * Clear the reserved location for the continuation pointer at
  * E[0]. This is not strictly necessary for correctness, but if a
  * GC is triggered before E[0] is overwritten by another
  * continuation pointer the now dead term at E[0] would be
  * retained by the GC.
  */
  E[0] = NIL;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_trim_yxt):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = yb(tmp_packed1&BEAM_TIGHT_MASK);
  do {
    E += tb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT)));

    /*
    * Clear the reserved location for the continuation pointer at
    * E[0]. This is not strictly necessary for correctness, but if a
    * GC is triggered before E[0] is overwritten by another
    * continuation pointer the now dead term at E[0] would be
    * retained by the GC.
    */
    E[0] = NIL;
  } while (0);
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move2_par_xxxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V1, V2;
  V1 = xb(tmp_packed1&BEAM_LOOSE_MASK);
  V2 = xb((tmp_packed1>>(2*BEAM_LOOSE_SHIFT))&BEAM_LOOSE_MASK);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)&BEAM_LOOSE_MASK) = V1;
  yb((tmp_packed1>>(3*BEAM_LOOSE_SHIFT))) = V2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move2_par_yxxx):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V1, V2;
  V1 = yb(tmp_packed1&BEAM_LOOSE_MASK);
  V2 = xb((tmp_packed1>>(2*BEAM_LOOSE_SHIFT))&BEAM_LOOSE_MASK);
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)&BEAM_LOOSE_MASK) = V1;
  xb((tmp_packed1>>(3*BEAM_LOOSE_SHIFT))) = V2;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_window2_xxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm xt0, xt1;
  Eterm* y = &yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT)));
  xt0  = xb(tmp_packed1&BEAM_TIGHT_MASK);
  xt1  = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  y[0] = xt0;
  y[1] = xt1;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(move_shift_xxy):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Eterm V;
  V = xb(tmp_packed1&BEAM_TIGHT_MASK);
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK);
  xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) = V;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(bs_init_writable):
{
  BeamInstr next_pf = BeamCodeAddr(I[1]);
  Eterm lb_binary;
  SWAPOUT;
  LbWritableStatus lb_status=lb_writable_init_term(c_p,x(0),&lb_binary);
  SWAPIN;
  FCALLS-=(Sint)c_p->last_native_cost;
  if (lb_status==LB_WRITABLE_NO_MEMORY) goto memory_failure;
  if (lb_status==LB_WRITABLE_LIMIT) goto resource_limit;
  if (lb_status!=LB_WRITABLE_OK) goto malformed_code;
  x(0)=lb_binary;
  I += 1;
  ASSERT(VALID_INSTR(next_pf));
  if (FCALLS<=0) goto context_switch3;
  GotoPF(next_pf);
}

OpCase(i_bs_create_bin_jIWdW):
{
  Eterm tmp_packed1 = I[2];
  Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  int n = I[4];
  const BeamInstr* p_start = I+5;
  const BeamInstr* p_end = p_start + n;
  const BeamInstr* p;
  Uint alloc = Ib(tmp_packed1&BEAM_WIDE_MASK);

  LbBinaryBuild build; LbBinaryBuild *EBS=&build;
  SWAPOUT;
  lb_binary_build_begin(c_p,p_start,n,alloc,I[3],&build);
  if (build.status!=LB_BUILD_OK) lb_binary_build_raise(c_p,&build);
  SWAPIN;
  if (build.status!=LB_BUILD_OK) {
    FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;
    if (I[1]) { I+=(Sint)I[1]; if (FCALLS<=0) goto context_switch3; Goto(*I); }
    goto find_func_info;
  }
  if (p_start[0]==LB_BSC_APPEND || p_start[0]==LB_BSC_PRIVATE_APPEND) p_start+=5;

  /* Construct the segments. */
  for (p = p_start; p < p_end; p += 5) {
    Eterm Src;
    Eterm Size;
    Eterm flags;
    Eterm unit;
    Sint _size;

    if(p[0] == LB_BSC_STRING) {
      const unsigned char* string;
      string = (const unsigned char *) p[3];;
      Size = p[4];;
      lb_build_string(EBS, string, Size);
      if (build.status!=LB_BUILD_OK) {
        FCALLS-=(Sint)build.cost;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;
      }
      continue;
    }

    Src = p[3];
    switch (loader_tag(Src)) {
      case LOADER_X_REG:
      Src = x(loader_x_reg_index(Src));
      break;
      case LOADER_Y_REG:
      Src = y(loader_y_reg_index(Src));
      break;
    };;

    switch (p[0]) {
      case LB_BSC_BINARY_ALL:
      unit = p[1];;
      if (!lb_build_binary_all(EBS, Src, unit)) {
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,am_unit,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_BINARY:
      unit = p[1];;
      flags = p[2];;
      Size = p[4];
      switch (loader_tag(Size)) {
        case LOADER_X_REG:
        Size = x(loader_x_reg_index(Size));
        break;
        case LOADER_Y_REG:
        Size = y(loader_y_reg_index(Size));
        break;
      };;
      if (is_small(Size)) {
        Uint uint_size;
        Sint signed_size = signed_val(Size);
        if (signed_size < 0) {
          c_p->freason = BADARG;

          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;;
        }
        uint_size = (Uint) signed_size;
        _size = uint_size * unit;
      } else {
        /*
        * On a 64-bit architecture, the size of any binary
        * that would fit in the memory fits in a small.
        */
        c_p->freason = BADARG;

        /*
        * In a correctly working program, we expect failures in
        * guards to be more likely than failures in bodies.
        */

        if (ERTS_LIKELY(I[1])) {
          ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
          I += (Sint)I[1];;
          if (FCALLS<=0) goto context_switch3; Goto(*I);;
        }
        goto find_func_info;;;
      };
      if (!lb_build_binary(EBS, Src, _size)) {
        Eterm reason = lb_build_is_bitstring(Src) ? am_short : am_type;
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,reason,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_BINARY_FIXED_SIZE:
      Size = p[4];;
      if (!lb_build_binary(EBS, Src, Size)) {
        Eterm reason = lb_build_is_bitstring(Src) ? am_short : am_type;
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,reason,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_FLOAT:
      unit = p[1];;
      flags = p[2];;
      Size = p[4];
      switch (loader_tag(Size)) {
        case LOADER_X_REG:
        Size = x(loader_x_reg_index(Size));
        break;
        case LOADER_Y_REG:
        Size = y(loader_y_reg_index(Size));
        break;
      };;
      if (is_small(Size)) {
        Uint uint_size;
        Sint signed_size = signed_val(Size);
        if (signed_size < 0) {
          c_p->freason = BADARG;

          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;;
        }
        uint_size = (Uint) signed_size;
        _size = uint_size * unit;
      } else {
        /*
        * On a 64-bit architecture, the size of any binary
        * that would fit in the memory fits in a small.
        */
        c_p->freason = BADARG;

        /*
        * In a correctly working program, we expect failures in
        * guards to be more likely than failures in bodies.
        */

        if (ERTS_LIKELY(I[1])) {
          ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
          I += (Sint)I[1];;
          if (FCALLS<=0) goto context_switch3; Goto(*I);;
        }
        goto find_func_info;;;
      };
      Src = lb_build_float(EBS, Src, _size, flags);
      if (is_value(Src)) {
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,build.float_info,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_FLOAT_FIXED_SIZE:
      flags = p[2];;
      Size = p[4];;
      Src = lb_build_float(EBS, Src, Size, flags);
      if (is_value(Src)) {
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,build.float_info,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_INTEGER:
      {
        Sint _size;
        int result;

        unit = p[1];;
        flags = p[2];;
        Size = p[4];
        switch (loader_tag(Size)) {
          case LOADER_X_REG:
          Size = x(loader_x_reg_index(Size));
          break;
          case LOADER_Y_REG:
          Size = y(loader_y_reg_index(Size));
          break;
        };;
        if (is_small(Size)) {
          Uint uint_size;
          Sint signed_size = signed_val(Size);
          if (signed_size < 0) {
            c_p->freason = BADARG;

            /*
            * In a correctly working program, we expect failures in
            * guards to be more likely than failures in bodies.
            */

            if (ERTS_LIKELY(I[1])) {
              ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
              I += (Sint)I[1];;
              if (FCALLS<=0) goto context_switch3; Goto(*I);;
            }
            goto find_func_info;;;
          }
          uint_size = (Uint) signed_size;
          _size = uint_size * unit;
        } else {
          /*
          * On a 64-bit architecture, the size of any binary
          * that would fit in the memory fits in a small.
          */
          c_p->freason = BADARG;

          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;;
        };
        if (flags & BSF_LITTLE) {
          result = lb_build_integer_le(EBS, Src, _size);
        } else {
          result = lb_build_integer_be(EBS, Src, _size);
        }
        if (!result) {
          do {
            SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,am_type,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


            /*
            * In a correctly working program, we expect failures in
            * guards to be more likely than failures in bodies.
            */

            if (ERTS_LIKELY(I[1])) {
              ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
              I += (Sint)I[1];;
              if (FCALLS<=0) goto context_switch3; Goto(*I);;
            }
            goto find_func_info;;
          } while (0);
        }
      }
      break;
      case LB_BSC_INTEGER_FIXED_SIZE:
      case LB_BSC_UTF32:
      {
        int result;

        flags = p[2];;
        Size = p[4];;
        if (flags & BSF_LITTLE) {
          result = lb_build_integer_le(EBS, Src, Size);
        } else {
          result = lb_build_integer_be(EBS, Src, Size);
        }
        if (!result) {
          do {
            SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,am_type,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


            /*
            * In a correctly working program, we expect failures in
            * guards to be more likely than failures in bodies.
            */

            if (ERTS_LIKELY(I[1])) {
              ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
              I += (Sint)I[1];;
              if (FCALLS<=0) goto context_switch3; Goto(*I);;
            }
            goto find_func_info;;
          } while (0);
        }
      }
      break;
      case LB_BSC_UTF8:
      if (!lb_build_utf8(EBS, Src)) {
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,am_type,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      case LB_BSC_UTF16:
      flags = p[2];;
      Src = p[3];
      switch (loader_tag(Src)) {
        case LOADER_X_REG:
        Src = x(loader_x_reg_index(Src));
        break;
        case LOADER_Y_REG:
        Src = y(loader_y_reg_index(Src));
        break;
      };;
      if (!lb_build_utf16(EBS, Src, flags)) {
        do {
          SWAPOUT; lb_binary_build_error(c_p,&build,p,BADARG,am_type,Src); SWAPIN;
FCALLS-=(Sint)build.cost; build.cost=0;
if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;
if (build.status==LB_BUILD_LIMIT) goto resource_limit;
if (build.status==LB_BUILD_REFUSED) goto execution_refused;
if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;
if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;


          /*
          * In a correctly working program, we expect failures in
          * guards to be more likely than failures in bodies.
          */

          if (ERTS_LIKELY(I[1])) {
            ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
            I += (Sint)I[1];;
            if (FCALLS<=0) goto context_switch3; Goto(*I);;
          }
          goto find_func_info;;
        } while (0);
      }
      break;
      default:
      ASSERT(0);
      break;
    }
  }

  FCALLS-=(Sint)build.cost; build.cost=0;

  /* Return the resulting binary. */
  dst_ptr = REG_TARGET_PTR(dst);
  if (build.status!=LB_BUILD_OK || build.writer.offset!=build.expected) goto malformed_code;
  *dst_ptr = build.result;
  I += n;
  I += 5;
  ASSERT(VALID_INSTR(*I));
  if (FCALLS<=0) goto context_switch3; Goto(*I);
}

OpCase(self_x):
{
  Eterm identity;
  LbProcessStatus identity_status=lb_process_identity(c_p,&identity);
  FCALLS-=(Sint)c_p->last_native_cost;
  if(identity_status==LB_PROCESS_NO_MEMORY) goto memory_failure;
  if(identity_status==LB_PROCESS_LIMIT) goto resource_limit;
  if(identity_status!=LB_PROCESS_READY) goto malformed_code;
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb(I[1]) = identity;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);
}

OpCase(self_y):
{
  Eterm identity;
  LbProcessStatus identity_status=lb_process_identity(c_p,&identity);
  FCALLS-=(Sint)c_p->last_native_cost;
  if(identity_status==LB_PROCESS_NO_MEMORY) goto memory_failure;
  if(identity_status==LB_PROCESS_LIMIT) goto resource_limit;
  if(identity_status!=LB_PROCESS_READY) goto malformed_code;
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  yb(I[1]) = identity;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);
}

OpCase(node_x):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  xb(I[1]) = c_p->entry_module->space->node_name;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(node_y):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  yb(I[1]) = c_p->entry_module->space->node_name;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(is_atom_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_atom(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_atom_fy):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_atom(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_pid_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_pid(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_reference_fx):
{
  Eterm tmp_packed1 = I[1];
  if (is_not_ref(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_call_fun_t):
{
  const BeamInstr *next;

  SWAPOUT; c_p->heap_reserved=0; next=lb_fun_call(c_p,I[1]); SWAPIN;
FCALLS-=(Sint)c_p->last_native_cost;
if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

  if (ERTS_UNLIKELY(next == NULL)) {
    goto find_func_info;;
  };
  ASSERT(VALID_INSTR(*(I+2)));
  *E = (BeamInstr) (I+2);;
  do {
    BeamInstr dis_next;

    SET_I(next);

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);ASSERT(!"Fell through 'i_call_fun' (-no_next)");
}

OpCase(i_call_fun_last_tQ):
{
  Eterm tmp_packed1 = I[1];
  const BeamInstr *next;

  SWAPOUT; c_p->heap_reserved=0; next=lb_fun_call(c_p,tb(tmp_packed1&BEAM_LOOSE_MASK)); SWAPIN;
FCALLS-=(Sint)c_p->last_native_cost;
if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;

  if (ERTS_UNLIKELY(next == NULL)) {
    goto find_func_info;;
  };
  E = ADD_BYTE_OFFSET(E, Qb((tmp_packed1>>BEAM_LOOSE_SHIFT)));;
  do {
    BeamInstr dis_next;

    SET_I(next);

    dis_next = *I;
    CHECK_ARGS(I);

    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(dis_next);
    } else {
      goto context_switch;
    }
  } while (0);ASSERT(!"Fell through 'i_call_fun_last' (-no_next)");
}

OpCase(i_make_fun3_Fdtt):
{
  Eterm tmp_packed1 = I[2];
  Eterm dst = db(tmp_packed1&BEAM_TIGHT_MASK);
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  ErlFunEntry *fe = (ErlFunEntry *) I[1];
  int i, num_free = tb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT)));
  ErlFunThing* funp;

  if(!lb_work_domain_charge(c_p->entry_module->space->domain,num_free+lb_work_bulk_units(num_free))) goto work_failure;
  LbAllocStatus held=lb_fun_retain(c_p,fe);
  FCALLS-=1+num_free;
  if(held==LB_ALLOC_NO_MEMORY) goto memory_failure;
  if(held!=LB_ALLOC_OK || (Uint)(E-HTOP)<ERL_FUN_SIZE+(Uint)num_free+S_RESERVED) goto malformed_code;
  funp = (ErlFunThing*)HTOP;
  HTOP += ERL_FUN_SIZE + num_free;

  funp->thing_word = MAKE_FUN_HEADER(tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK), num_free, 0);
  funp->entry.fun = fe;

#ifdef DEBUG
  {
    /* Note that `mfa` may be NULL if the fun is currently being purged. We
    * ignore this since it's not an error and we only need `mfa` to
    * sanity-check the arity at this point. If the fun is called while in
    * this state, the `error_handler` module will take care of it. */
    LbMFA actual_mfa; memcpy(&actual_mfa,fe->dispatch.addresses[0]-sizeof(LbMFA)/sizeof(BeamInstr),sizeof(actual_mfa));
    const LbMFA *mfa=&actual_mfa;
    ASSERT(!mfa || fun_arity(funp) == mfa->arity - num_free);
    ASSERT(tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) == (Uint)fe->arity);
  }
#endif

  I = I+3;

  for (i = 0; i < num_free; i++) {
    Eterm term = *I++;
    switch (loader_tag(term)) {
      case LOADER_X_REG:
      term = x(loader_x_reg_index(term));
      break;
      case LOADER_Y_REG:
      term = y(loader_y_reg_index(term));
      break;
    }
    funp->env[i] = term;
  }

  *dst_ptr = make_fun(funp);
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);ASSERT(!"Fell through 'i_make_fun3' (-no_next)");
}

OpCase(send):
{
  BeamInstr next_pf = BeamCodeAddr(I[1]);
  Eterm result;

  if (!(FCALLS > 0 || FCALLS > neg_o_reds)) {
    /* If we have run out of reductions, we do a context
    switch before calling the bif */
    c_p->arity = 2;
    c_p->current = NULL;
    goto context_switch3;
  }

  SWAPOUT;

  result = erl_send(c_p, x(0), x(1));



  SWAPIN;
  FCALLS-=1+(Sint)c_p->last_native_cost;
  if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
  if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
  if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
  if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;
  if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;
  if (ERTS_LIKELY(is_value(result))) {
    x(0) = result;
    CHECK_TERM(x(0));
  } else if (c_p->freason == TRAP) {
    ASSERT(VALID_INSTR(*(I+1)));
    *E = (BeamInstr) (I+1);;
    SET_I(c_p->i);
    SWAPIN;
    do {
      BeamInstr dis_next;

      dis_next = *I;
      CHECK_ARGS(I);

      if (FCALLS > 0 || FCALLS > neg_o_reds) {
        FCALLS--;
        Goto(dis_next);
      } else {
        goto context_switch;
      }
    } while (0);
  } else {
    goto find_func_info;
  }
  I += 1;
  ASSERT(VALID_INSTR(next_pf));
  if(FCALLS<=0) goto context_switch3;
  GotoPF(next_pf);
}

OpCase(recv_marker_reserve_S):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  SWAPOUT;

  Eterm lb_marker;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure;
  LbProcessStatus lb_marker_status=lb_receive_marker_reserve(c_p,&lb_marker);
  if(lb_marker_status==LB_PROCESS_NO_MEMORY) goto memory_failure;
  if(lb_marker_status==LB_PROCESS_LIMIT) goto resource_limit;
  if(lb_marker_status!=LB_PROCESS_READY) goto malformed_code;
  *REG_TARGET_PTR(I[1])=lb_marker;

  SWAPIN;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(recv_marker_bind_SS):
{
  Eterm tmp_packed1 = I[1];
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_bind(c_p, Sb(tmp_packed1&BEAM_LOOSE_MASK), Sb((tmp_packed1>>BEAM_LOOSE_SHIFT)));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(recv_marker_clear_S):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_clear(c_p, Sb(I[1]));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(recv_marker_use_S):
{
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_use(c_p, Sb(I[1]));
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(i_loop_rec_f):
{

  /*
  * Pick up the next message and place it in x(0).
  * If no message, jump to a wait or wait_timeout instruction.
  */

  LbMessage* msgp;

  /*
  * We need to disable GC while matching messages
  * in the queue. This since messages with data outside
  * the heap will be corrupted by a GC.
  */
  ASSERT(!(c_p->flags & F_DELAY_GC));
  c_p->flags |= F_DELAY_GC;

  /* Entry point from loop_rec_end (and locally) */
  loop_rec__:

  if (FCALLS <= 0 && FCALLS <= neg_o_reds) {
    c_p->i = I;
    ASSERT(VALID_INSTR(*(const BeamInstr*)c_p->i));;
    c_p->flags &= ~F_DELAY_GC;
    SWAPOUT;
    c_p->arity = 0;
    c_p->current = NULL;
    goto context_switch3;
  }

  ASSERT(!(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED));



  msgp = (*c_p->messages.save);

  if (ERTS_UNLIKELY(msgp == NULL)) {
    int get_out;
    SWAPOUT;
    c_p->i = I;
    ASSERT(VALID_INSTR(*(const BeamInstr*)c_p->i));;
    c_p->arity = 0;
    c_p->current = NULL;
    c_p->fcalls = FCALLS;

    get_out=0; msgp=*c_p->messages.save;

    SWAPIN;
    if (ERTS_UNLIKELY(msgp == NULL)) {
      if (get_out) {
        if (get_out < 0) {
          ASSERT(FCALLS <= 0 && FCALLS <= neg_o_reds);
          goto loop_rec__; /* yield */
        }
        else {
          ASSERT((c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED));
          goto context_switch3; /* exit */
        }
      }

      /*
      * If there are no more messages in queue
      * (and we are not yielding or exiting)
      * erts_proc_sig_receive_helper()
      * returns with message queue lock locked...
      */
      c_p->flags &= ~F_DELAY_GC;
      ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
      I += (Sint)I[1];;
      Goto(*I);		/* Jump to a wait or wait_timeout instruction */
    }
  }

  ASSERT(msgp == (*c_p->messages.save));
  ASSERT(msgp && (msgp->queue==&c_p->messages));

  if (ERTS_UNLIKELY(is_non_value(msgp->m[0]))) {
    FCALLS -= 10; /* FIXME: bump appropriate amount... */
    SWAPOUT; /* erts_proc_sig_decode_dist() may write to heap... */
    goto execution_refused;
    SWAPIN;
  }

  ASSERT(msgp == (*c_p->messages.save));
  ASSERT(!is_non_value(msgp->m[0]));

  SWAPOUT;
  Eterm lb_view;
  LbProcessStatus lb_receive_status=lb_message_peek_owned(c_p,&lb_view);
  SWAPIN;
  if(lb_receive_status==LB_PROCESS_NO_MEMORY) goto memory_failure;
  if(lb_receive_status==LB_PROCESS_LIMIT) goto resource_limit;
  if(lb_receive_status==LB_PROCESS_WORK_LIMIT) goto work_failure;
  if(lb_receive_status!=LB_PROCESS_READY || is_non_value(lb_view)) goto malformed_code;
  FCALLS-=(Sint)c_p->last_native_cost;
  x(0)=lb_view;
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(loop_rec_end_f):
{
  /*
  * Advance the save pointer to the next message (the current
  * message didn't match), then jump to the loop_rec instruction.
  */

  ASSERT(c_p->flags & F_DELAY_GC);

  ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
  I += (Sint)I[1];;
  lb_message_skip_owned(c_p);
  FCALLS--;
  goto loop_rec__;ASSERT(!"Fell through 'loop_rec_end' (-no_next)");
}

OpCase(remove_message):
{

  /*
  * Remove a (matched) message from the message queue.
  */

  LbMessage* msgp;




  msgp = (*c_p->messages.save);

  if (msgp->m[1]!=NIL) goto execution_refused;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure;
  if(msgp!=c_p->receive_message || lb_message_queue_take(&c_p->messages)!=msgp) goto malformed_code;
  c_p->messages.save=&c_p->messages.first;
  c_p->receive_message=NULL; c_p->receive_root=NIL;
  lb_work_receive_end(c_p,1);
  lb_receive_timer_cancel(c_p);

  lb_message_destroy(msgp);
  c_p->flags &= ~F_DELAY_GC;

  if ((c_p->off_heap.overhead>=c_p->bin_vheap_sz)) {
    /*
    * We want to GC soon but we leave a few
    * reductions giving the message some time
    * to turn into garbage.
    */
    if(FCALLS>5) FCALLS=5;
  }






  I += 1;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(timeout):
{
  BeamInstr next_pf = BeamCodeAddr(I[1]);


  lb_receive_timer_cancel(c_p);
  c_p->messages.save=&c_p->messages.first;
  I += 1;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(timeout_locked):
{
  BeamInstr next_pf = BeamCodeAddr(I[1]);
  /*
  * A timeout has occurred.  Reset the save pointer so that the next
  * receive statement will examine the first message first.
  */




  lb_receive_timer_cancel(c_p);
  c_p->messages.save=&c_p->messages.first;;
  I += 1;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(bs_test_tail_imm2_fxW):
{
  Eterm tmp_packed1 = I[1];
  ErlSubBits *sb = (ErlSubBits*)bitstring_val(xb((tmp_packed1>>BEAM_WIDE_SHIFT)));
  if (sb->end - sb->start != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(bs_test_tail_imm2_fyW):
{
  Eterm tmp_packed1 = I[1];
  ErlSubBits *sb = (ErlSubBits*)bitstring_val(yb((tmp_packed1>>BEAM_WIDE_SHIFT)));
  if (sb->end - sb->start != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(bs_test_zero_tail2_fx):
{
  Eterm tmp_packed1 = I[1];
  ErlSubBits *sb = (ErlSubBits*)bitstring_val(xb((tmp_packed1>>BEAM_WIDE_SHIFT)));
  if (sb->end != sb->start) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(bs_test_zero_tail2_fy):
{
  Eterm tmp_packed1 = I[1];
  ErlSubBits *sb = (ErlSubBits*)bitstring_val(yb((tmp_packed1>>BEAM_WIDE_SHIFT)));
  if (sb->end != sb->start) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_bs_eq_ftW):
{
  Eterm tmp_packed1 = I[1];
  Uint size = tb((tmp_packed1>>BEAM_WIDE_SHIFT));
  Eterm result;

  result = bitdata >> (8 * sizeof(Uint) - size);
  bitdata <<= size;
  if (result != I[2]) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_bs_extract_integer_td):
{
  Eterm tmp_packed1 = I[1];
  Eterm dst = db((tmp_packed1>>BEAM_LOOSE_SHIFT));
  Eterm* dst_ptr = REG_TARGET_PTR(dst);
  BeamInstr next_pf = BeamCodeAddr(I[2]);
  Uint size = tb(tmp_packed1&BEAM_LOOSE_MASK);
  Eterm result;

  result = bitdata >> (8 * sizeof(Uint) - size);
  result = make_small(result);
  bitdata <<= size;
  *dst_ptr = result;
  I += 2;
  ASSERT(VALID_INSTR(next_pf));
  GotoPF(next_pf);
}

OpCase(is_function_fx):
{
  Eterm tmp_packed1 = I[1];
  if ( !(is_any_fun(xb((tmp_packed1>>BEAM_WIDE_SHIFT)))) ) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(is_function_fy):
{
  Eterm tmp_packed1 = I[1];
  if ( !(is_any_fun(yb((tmp_packed1>>BEAM_WIDE_SHIFT)))) ) {
    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;
  }
  I += 2;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(hot_is_function2_fSt):
{
  Eterm tmp_packed1 = I[2];
  if (!is_function2(Sb(tmp_packed1&BEAM_LOOSE_MASK), tb((tmp_packed1>>BEAM_LOOSE_SHIFT)))) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(cold_is_function2_fxx):
{
  Eterm tmp_packed1 = I[2];
  if (erl_is_function(c_p, xb(tmp_packed1&BEAM_LOOSE_MASK), xb((tmp_packed1>>BEAM_LOOSE_SHIFT))) != am_true ) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  I += 3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(new_map_dtI):
{
  Eterm packed=I[1], dst=db(packed&BEAM_LOOSE_MASK), result;
  Uint words=I[2], live=tb(packed>>BEAM_LOOSE_SHIFT);
  if((words&1) || words>2*LB_FLATMAP_MAX_SIZE || live>MAX_REG) goto malformed_code;
  size_t count=words/2;
  size_t reserve=LB_FLATMAP_WORDS+count+(count?count+1:2);
  c_p->last_native_cost=0; SWAPOUT;
  LbMapStatus status=lb_map_reserve(c_p,reserve,live);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE];
  for(size_t i=0;i<count;++i) { GetSource(I[3+2*i],keys[i]); GetSource(I[4+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_create(c_p,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=3+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}

OpCase(i_new_small_map_lit_dtq):
{
  Eterm packed=I[1], dst=db(packed&BEAM_LOOSE_MASK), keys=I[2], result;
  Uint live=tb(packed>>BEAM_LOOSE_SHIFT);
  if(!is_tuple(keys) || live>MAX_REG) goto malformed_code;
  size_t count=(size_t)arityval(*tuple_val(keys));
  if(count>LB_FLATMAP_MAX_SIZE) goto execution_refused;
  size_t reserve=LB_FLATMAP_WORDS+count+(count?count+1:2);
  c_p->last_native_cost=0; SWAPOUT;
  LbMapStatus status=lb_map_reserve(c_p,reserve,live);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  Eterm values[LB_FLATMAP_MAX_SIZE];
  for(size_t i=0;i<count;++i) GetSource(I[3+i],values[i]);
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_create_literal(c_p,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=3+count;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}

OpCase(i_get_map_elements_fsI):
{
  Eterm packed=I[1], map;
  GetSource(I[2],map);
  Uint words=Ib(packed>>BEAM_WIDE_SHIFT);
  if(words%3 || words/3>LB_FLATMAP_MAX_SIZE) goto malformed_code;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
  FCALLS--;
  int is_map=0;
  LbMapStatus status=lb_map_is_map(map,&is_map);
  MAP_STATUS(status);
  if(!is_map) { I+=fb(packed&BEAM_WIDE_MASK); Goto(*I); }
  const BeamInstr *args=I+3;
  Uint count=words/3;
  for(Uint i=0;i<count;++i) {
    Eterm value,key;
    GetSource(args[0],key);
    c_p->last_native_cost=0;
    status=lb_map_find(c_p,map,key,&value);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(status==LB_MAP_MISSING) { I+=fb(packed&BEAM_WIDE_MASK); Goto(*I); }
    MAP_STATUS(status);
    MAP_PUT_TERM_REG(value,args[1]);
    args+=3;
  }
  I+=3+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}

OpCase(is_map_fx):
{
  Eterm packed=I[1],value=xb(packed>>BEAM_WIDE_SHIFT);
  Sint fail=fb(packed&BEAM_WIDE_MASK); int is_map=0;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
  FCALLS--;
  LbMapStatus status=lb_map_is_map(value,&is_map); MAP_STATUS(status);
  if(!is_map) { I+=fail; Goto(*I); }
  I+=2; if(FCALLS<=0) goto context_switch3; Goto(*I);
}

OpCase(is_map_fy):
{
  Eterm packed=I[1],value=yb(packed>>BEAM_WIDE_SHIFT);
  Sint fail=fb(packed&BEAM_WIDE_MASK); int is_map=0;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
  FCALLS--;
  LbMapStatus status=lb_map_is_map(value,&is_map); MAP_STATUS(status);
  if(!is_map) { I+=fail; Goto(*I); }
  I+=2; if(FCALLS<=0) goto context_switch3; Goto(*I);
}

OpCase(i_get_map_element_fxxx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fxxy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fxyx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fxyy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fyxx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fyxy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=xb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fyyx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_fyyy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_TIGHT_MASK); Eterm lb_key=yb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK); Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>(2*BEAM_TIGHT_SHIFT))) = res;
  I += 3; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_hash_fxcWx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_LOOSE_MASK); Eterm lb_key=I[3]; Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = res;
  I += 5; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_hash_fxcWy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=xb(tmp_packed1&BEAM_LOOSE_MASK); Eterm lb_key=I[3]; Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = res;
  I += 5; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_hash_fycWx):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_LOOSE_MASK); Eterm lb_key=I[3]; Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  xb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = res;
  I += 5; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

OpCase(i_get_map_element_hash_fycWy):
{
  Eterm tmp_packed1 = I[2];
  Eterm lb_map_term=yb(tmp_packed1&BEAM_LOOSE_MASK); Eterm lb_key=I[3]; Eterm res; int lb_is_map=0;
  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);
  if(!lb_is_map) res=THE_NON_VALUE; else {
    c_p->last_native_cost=0;
    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);
  }
  if (is_non_value(res)) {
    ASSERT(VALID_INSTR(*(I + (Sint)I[1])));
    I += (Sint)I[1];;
    Goto(*I);;
  }
  yb((tmp_packed1>>BEAM_LOOSE_SHIFT)) = res;
  I += 5; if(FCALLS<=0) goto context_switch3;
  ASSERT(VALID_INSTR(*I));
  Goto(*I);
}

{
  Uint num_bytes;
  OpCase(deallocate_return0):
  {
    num_bytes = (0+1) * sizeof(Eterm);
  }
  goto dealloc_ret__execute;

  OpCase(deallocate_return1):
  {
    num_bytes = (1+1) * sizeof(Eterm);
  }
  goto dealloc_ret__execute;

  OpCase(deallocate_return2):
  {
    num_bytes = (2+1) * sizeof(Eterm);
  }
  goto dealloc_ret__execute;

  OpCase(deallocate_return3):
  {
    num_bytes = (3+1) * sizeof(Eterm);
  }
  goto dealloc_ret__execute;

  OpCase(deallocate_return4):
  {
    num_bytes = (4+1) * sizeof(Eterm);
  }
  goto dealloc_ret__execute;

  OpCase(deallocate_return_Q):
  {
    num_bytes = I[1];
  }
  I += 1;
  goto dealloc_ret__execute;

  dealloc_ret__execute:
  {

    E = ADD_BYTE_OFFSET(E, num_bytes);
    SET_I(cp_val(*E));
    *E = NIL;;
    CHECK_TERM(x(0));
    if (FCALLS > 0 || FCALLS > neg_o_reds) {
      FCALLS--;
      Goto(*I);
    } else {
      c_p->current = NULL;
      c_p->arity = 1;
      goto context_switch3;
    };ASSERT(!"Fell through 'dealloc_ret.execute' (-no_next)");
  }

}

{
  Eterm select_val2;
  OpCase(i_select_tuple_arity2_xfAA):
  {
    select_val2 = xb(I[1]);
  }
  goto select_val2__get_arity;

  OpCase(i_select_tuple_arity2_yfAA):
  {
    select_val2 = yb(I[1]);
  }
  goto select_val2__get_arity;

  OpCase(i_select_val2_xfcc):
  {
    select_val2 = xb(I[1]);
  }
  goto select_val2__execute;

  OpCase(i_select_val2_yfcc):
  {
    select_val2 = yb(I[1]);
  }
  goto select_val2__execute;

  select_val2__get_arity:
  {
    if (ERTS_LIKELY(is_tuple(select_val2))) {
      select_val2 = *tuple_val(select_val2);
    } else {
      select_val2 = NIL;
    }
  }
  goto select_val2__execute;

  select_val2__execute:
  {
    const BeamInstr* jump_tab = (I+5);

    if (select_val2 == I[3]) {
      ASSERT(VALID_INSTR(*(I + (lb_packed_label(jump_tab, 0)) + 0)));
      I += lb_packed_label(jump_tab, 0) + 0;;
      Goto(*I);;
    } else if (select_val2 == I[4]) {
      ASSERT(VALID_INSTR(*(I + (lb_packed_label(jump_tab, 1)) + 0)));
      I += lb_packed_label(jump_tab, 1) + 0;;
      Goto(*I);;
    } else {
      ASSERT(VALID_INSTR(*(I + (Sint)I[2])));
      I += (Sint)I[2];;
      Goto(*I);;
    }ASSERT(!"Fell through 'select_val2.execute' (-no_next)");
  }

}

{
  Eterm select_val;
  OpCase(i_select_tuple_arity_xfI):
  {
    select_val = xb(I[1]);
  }
  goto select_val_lin__get_arity;

  OpCase(i_select_tuple_arity_yfI):
  {
    select_val = yb(I[1]);
  }
  goto select_val_lin__get_arity;

  OpCase(i_select_val_lins_xfI):
  {
    select_val = xb(I[1]);
  }
  goto select_val_lin__execute;

  OpCase(i_select_val_lins_yfI):
  {
    select_val = yb(I[1]);
  }
  goto select_val_lin__execute;

  select_val_lin__get_arity:
  {
    if (ERTS_LIKELY(is_tuple(select_val))) {
      select_val = *tuple_val(select_val);
    } else {
      select_val = NIL;
    }
  }
  goto select_val_lin__execute;

  select_val_lin__execute:
  {
    Eterm tmp_packed1 = I[2];
    const BeamInstr* vs = I+3;
    int ix = 0;

    for (;;) {
      if(!lb_work_domain_charge(c_p->entry_module->space->domain,2)) goto work_failure;
      if (vs[ix+0] >= select_val) {
        ix += 0;
        break;
      }
      if (vs[ix+1] >= select_val) {
        ix += 1;
        break;
      }
      ix += 2;
    }

    if (vs[ix] == select_val) {
      const BeamInstr* jump_tab = (I+3 + Ib((tmp_packed1>>BEAM_WIDE_SHIFT)));
      Eterm offset = lb_packed_label(jump_tab, ix);
      ASSERT(VALID_INSTR(*(I + (Sint)offset)));
      I += (Sint)offset;;
      Goto(*I);;
    } else {
      ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
      I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
      Goto(*I);;
    }ASSERT(!"Fell through 'select_val_lin.execute' (-no_next)");
  }

}

{
  Eterm increment_reg_val;
  OpCase(i_increment_rWd):
  {
    increment_reg_val = x(0);
  }
  goto increment__execute;

  OpCase(i_increment_xWd):
  {
    increment_reg_val = xb(I[1]);
  }
  I += 1;
  goto increment__execute;

  OpCase(i_increment_yWd):
  {
    increment_reg_val = yb(I[1]);
  }
  I += 1;
  goto increment__execute;

  increment__execute:
  {
    Eterm dst = I[2];
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    Eterm increment_val = I[1];
    Eterm result;

    if (ERTS_LIKELY(is_small(increment_reg_val))) {
#ifdef HAVE_OVERFLOW_CHECK_BUILTINS
      Sint lhs_tagged, rhs_untagged, res;

      /* See plus.execute */
      lhs_tagged = increment_reg_val;
      rhs_untagged = (Sint)((Uint)increment_val << _TAG_IMMED1_SIZE);

      if (ERTS_LIKELY(!__builtin_add_overflow(lhs_tagged, rhs_untagged, &res))) {
        ASSERT(is_small(res));
        *dst_ptr = res;
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
#else
      Sint i = signed_val(increment_reg_val) + increment_val;
      if (ERTS_LIKELY(IS_SSMALL(i))) {
        *dst_ptr = make_small(i);
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
#endif
    }

    SWAPOUT; result = lb_mixed_plus(c_p, increment_reg_val, make_small(increment_val)); SWAPIN;
      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;
      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;
      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;

    if (ERTS_LIKELY(is_value(result))) {
      *dst_ptr = result;
      SET_I((BeamInstr *) I+3);
      Goto(*I);;
    }
    ASSERT(c_p->freason != BADMATCH || is_value(c_p->fvalue));
    goto find_func_info;ASSERT(!"Fell through 'increment.execute' (-no_next)");
  }

}

{
  Eterm PlusOp1, PlusOp2;
  OpCase(i_plus_xcjd):
  {
    PlusOp1 = xb(I[1]);
    PlusOp2 = I[2];
  }
  I += 1;
  goto plus__execute;

  OpCase(i_plus_xxjd):
  {
    Eterm tmp_packed1 = I[1];
    PlusOp1 = xb(tmp_packed1&BEAM_LOOSE_MASK);
    PlusOp2 = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto plus__execute;

  OpCase(i_plus_xyjd):
  {
    Eterm tmp_packed1 = I[1];
    PlusOp1 = xb(tmp_packed1&BEAM_LOOSE_MASK);
    PlusOp2 = yb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto plus__execute;

  OpCase(i_plus_ycjd):
  {
    PlusOp1 = yb(I[1]);
    PlusOp2 = I[2];
  }
  I += 1;
  goto plus__execute;

  OpCase(i_plus_yxjd):
  {
    Eterm tmp_packed1 = I[1];
    PlusOp1 = yb(tmp_packed1&BEAM_LOOSE_MASK);
    PlusOp2 = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto plus__execute;

  OpCase(i_plus_yyjd):
  {
    Eterm tmp_packed1 = I[1];
    PlusOp1 = yb(tmp_packed1&BEAM_LOOSE_MASK);
    PlusOp2 = yb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto plus__execute;

  plus__execute:
  {
    Eterm tmp_packed1 = I[2];
    Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    if (ERTS_LIKELY(is_both_small(PlusOp1, PlusOp2))) {
#ifdef HAVE_OVERFLOW_CHECK_BUILTINS
      Sint lhs_tagged, rhs_untagged, res;

      /* The value part of immediate integers start right after the tag and
      * occupy the rest of the word, so if you squint a bit they look like
      * fixed-point integers; as long as you mask the tag away you will get
      * correct results from addition/subtraction since they share the same
      * notion of zero. It's fairly easy to see that the following holds
      * when (a + b) is in range:
      *
      *     (a >> s) + (b >> s) == ((a & ~m) + (b & ~m)) >> s
      *
      * Where 's' is the tag size and 'm' is the tag mask.
      *
      * The left-hand side is our fallback in the #else clause and is the
      * fastest way to do this safely in plain C. The actual addition will
      * never overflow since `Sint` has a much greater range than our
      * smalls, so we can use the IS_SSMALL macro to see if the result is
      * within range.
      *
      * What we're doing below is an extension of the right-hand side. By
      * treating `a` and `b` as fixed-point integers, all additions whose
      * result is out of range will also overflow `Sint` and we can use the
      * compiler's overflow intrinsics to check for this condition.
      *
      * In addition, since the tag lives in the lowest bits we can further
      * optimize this by only stripping the tag from either side. The higher
      * bits can't influence the tag bits since we bail on overflow, so the
      * tag bits from the tagged side will simply appear in the result. */
      lhs_tagged = PlusOp1;
      rhs_untagged = PlusOp2 & ~_TAG_IMMED1_MASK;

      if (ERTS_LIKELY(!__builtin_add_overflow(lhs_tagged, rhs_untagged, &res))) {
        ASSERT(is_small(res));
        *dst_ptr = res;
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
#else
      Sint i = signed_val(PlusOp1) + signed_val(PlusOp2);
      if (ERTS_LIKELY(IS_SSMALL(i))) {
        *dst_ptr = make_small(i);
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
#endif
    }
    do {
      Eterm result;



      SWAPOUT; result = lb_mixed_plus(c_p, PlusOp1, PlusOp2); SWAPIN;
      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;
      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;
      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;




      if (ERTS_LIKELY(is_value(result))) {
        *dst_ptr = result;
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
      if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
        ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
        I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
        Goto(*I);;
      }
      reg[0] = PlusOp1;
      reg[1] = PlusOp2;
      SWAPOUT;
      goto find_func_info;;
    } while (0);ASSERT(!"Fell through 'plus.execute' (-no_next)");
  }

}

{
  Eterm RemOp1, RemOp2;
  OpCase(i_rem_ssjd):
  {
    Eterm targ1;
    Eterm targ2;
    GetSource(I[1], targ1);
    GetSource(I[2], targ2);
    {
      RemOp1 = targ1;
      RemOp2 = targ2;
    }
  }
  I += 1;
  goto rem__execute;

  OpCase(i_rem_xxjd):
  {
    Eterm tmp_packed1 = I[1];
    RemOp1 = xb(tmp_packed1&BEAM_LOOSE_MASK);
    RemOp2 = xb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto rem__execute;

  rem__execute:
  {
    Eterm tmp_packed1 = I[2];
    Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    if (ERTS_UNLIKELY(RemOp2 == SMALL_ZERO)) {
      c_p->freason = BADARITH;
      if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
        ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
        I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
        Goto(*I);;
      }
      reg[0] = RemOp1;
      reg[1] = RemOp2;
      SWAPOUT;
      goto find_func_info;;
    } else if (ERTS_LIKELY(is_both_small(RemOp1, RemOp2))) {
      Sint lhs_untagged, rhs_untagged, untagged_result;

      /* See plus.execute */
      lhs_untagged = (RemOp1 & ~_TAG_IMMED1_MASK);
      rhs_untagged = (RemOp2 & ~_TAG_IMMED1_MASK);
      untagged_result = lhs_untagged % rhs_untagged;

      *dst_ptr = untagged_result | _TAG_IMMED1_SMALL;
      SET_I((BeamInstr *) I+3);
      Goto(*I);;
    } else {
      do {
        int succeeded;
        Eterm q, r;



        SWAPOUT; succeeded = lb_int_div_rem(c_p, RemOp1, RemOp2, &q, &r); SWAPIN;
        dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
        if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
        if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
        if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
        if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;




        if (ERTS_LIKELY(succeeded)) {
          *dst_ptr = r;
          SET_I((BeamInstr *) I+3);
          Goto(*I);;
        }
        if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
          ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
          I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
          Goto(*I);;
        }
        reg[0] = RemOp1;
        reg[1] = RemOp2;
        SWAPOUT;
        goto find_func_info;;
      } while (0);
    }ASSERT(!"Fell through 'rem.execute' (-no_next)");
  }

}

{
  Eterm MinusOp;
  OpCase(i_unary_minus_cjd):
  {
    MinusOp = I[1];
  }
  goto unary_minus__execute;

  OpCase(i_unary_minus_xjd):
  {
    MinusOp = xb(I[1]);
  }
  goto unary_minus__execute;

  OpCase(i_unary_minus_yjd):
  {
    MinusOp = yb(I[1]);
  }
  goto unary_minus__execute;

  unary_minus__execute:
  {
    Eterm tmp_packed1 = I[2];
    Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    if (ERTS_LIKELY(is_small(MinusOp))) {
      Sint i = -signed_val(MinusOp);
      if (ERTS_LIKELY(IS_SSMALL(i))) {
        *dst_ptr = make_small(i);
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
    }
    do {
      Eterm result;



      SWAPOUT; result = lb_unary_minus(c_p, MinusOp); SWAPIN;
      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;
      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;
      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;
      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;




      if (ERTS_LIKELY(is_value(result))) {
        *dst_ptr = result;
        SET_I((BeamInstr *) I+3);
        Goto(*I);;
      }
      if (ERTS_LIKELY(jb(tmp_packed1&BEAM_WIDE_MASK))) {
        ASSERT(VALID_INSTR(*(I + (jb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
        I += jb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
        Goto(*I);;
      }
      reg[0] = MinusOp;
      SWAPOUT;
      goto find_func_info;;
    } while (0);ASSERT(!"Fell through 'unary_minus.execute' (-no_next)");
  }

}

{
  ErtsBifFunc bf;
  Eterm tmp_reg[3]={NIL,NIL,NIL};
  OpCase(i_bif3_sssfbd):
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[2] = targ1;
    }
  }
  I += 1;
  goto i_bif__fetch1;

  OpCase(i_bif2_ssfbd):
  i_bif__fetch1:
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[1] = targ1;
    }
  }
  I += 1;
  goto i_bif__fetch0;

  OpCase(i_bif1_sfbd):
  i_bif__fetch0:
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[0] = targ1;
    }
  }
  goto i_bif__call;

  i_bif__call:
  {
    Eterm tmp_packed1 = I[2];
    Eterm dst = db((tmp_packed1>>BEAM_WIDE_SHIFT));
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    bf = (BifFunction) I[3];
    do {
      Eterm result;
Eterm* orig_htop=HTOP; Eterm* orig_stop=E;






      SWAPOUT;
      c_p->last_native_cost=0; result = (*bf)(c_p, tmp_reg, I);
      FCALLS-=(Sint)c_p->last_native_cost;
      if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
      if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;
      if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
      if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
      if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;
      SWAPIN;
      if(bf!=lb_bif_abs_1 && bf!=lb_bif_floor_1 && bf!=lb_bif_ceil_1 && bf!=lb_bif_trunc_1 && bf!=lb_bif_round_1 && bf!=lb_bif_float_1 && (orig_htop!=HTOP || orig_stop!=E)) goto malformed_code;
      dst_ptr=REG_TARGET_PTR(dst);







      if (ERTS_LIKELY(is_value(result))) {
        *dst_ptr = result;
        SET_I((BeamInstr *) I+4); if(FCALLS<=0) goto context_switch3;
        Goto(*I);;
      }
    } while (0);

    ASSERT(VALID_INSTR(*(I + (fb(tmp_packed1&BEAM_WIDE_MASK)) + 0)));
    I += fb(tmp_packed1&BEAM_WIDE_MASK) + 0;;
    Goto(*I);;ASSERT(!"Fell through 'i_bif.call' (-no_next)");
  }

}

{
  ErtsBifFunc bf;
  Eterm tmp_reg[3]={NIL,NIL,NIL};
  OpCase(i_bif3_body_sssbd):
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[2] = targ1;
    }
  }
  I += 1;
  goto i_bif_body__fetch1;

  OpCase(i_bif2_body_ssbd):
  i_bif_body__fetch1:
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[1] = targ1;
    }
  }
  I += 1;
  goto i_bif_body__fetch0;

  OpCase(i_bif1_body_sbd):
  i_bif_body__fetch0:
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      tmp_reg[0] = targ1;
    }
  }
  goto i_bif_body__call;

  i_bif_body__call:
  {
    Eterm dst = I[3];
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    bf = (BifFunction) I[2];
    do {
      Eterm result;
Eterm* orig_htop=HTOP; Eterm* orig_stop=E;






      SWAPOUT;
      c_p->last_native_cost=0; result = (*bf)(c_p, tmp_reg, I);
      FCALLS-=(Sint)c_p->last_native_cost;
      if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;
      if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;
      if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;
      if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;
      if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;
      if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;
      if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;
      SWAPIN;
      if(bf!=lb_bif_abs_1 && bf!=lb_bif_floor_1 && bf!=lb_bif_ceil_1 && bf!=lb_bif_trunc_1 && bf!=lb_bif_round_1 && bf!=lb_bif_float_1 && (orig_htop!=HTOP || orig_stop!=E)) goto malformed_code;
      dst_ptr=REG_TARGET_PTR(dst);







      if (ERTS_LIKELY(is_value(result))) {
        *dst_ptr = result;
        SET_I((BeamInstr *) I+4); if(FCALLS<=0) goto context_switch3;
        Goto(*I);;
      }
    } while (0);

    reg[0] = tmp_reg[0];
    reg[1] = tmp_reg[1];
    reg[2] = tmp_reg[2];
    SWAPOUT;
    goto find_func_info;ASSERT(!"Fell through 'i_bif_body.call' (-no_next)");
  }

}

{
  OpCase(wait_locked_f):
  {

  }
  goto wait__execute;

  OpCase(wait_timeout_unlocked_int_If):
  {

  }
  goto wait__int;

  OpCase(wait_timeout_unlocked_sf):
  {

  }
  goto wait__src;

  OpCase(wait_unlocked_f):
  {

  }
  goto wait__execute;

  OpCase(wait_timeout_locked_int_If):
  wait__int:
  {
    /*
    * If we have already set the timer, we must NOT set it again.  Therefore,
    * we must test the F_INSLPQUEUE flag as well as the F_TIMO flag.
    */
    if ((c_p->flags & (F_INSLPQUEUE | F_TIMO)) == 0) {
      c_p->receive_timeout_pc=I+3;
      LbProcessStatus lb_timer_status=lb_receive_timer_set(c_p,I[1]);
      if(lb_timer_status==LB_PROCESS_LIMIT) goto resource_limit;
      if(lb_timer_status!=LB_PROCESS_READY) goto environment_failure;
    }
  }
  I += 1;
  goto wait__execute;

  OpCase(wait_timeout_locked_sf):
  wait__src:
  {
    Eterm targ1;
    GetSource(I[1], targ1);
    {
      /*
      * If we have already set the timer, we must NOT set it again.  Therefore,
      * we must test the F_INSLPQUEUE flag as well as the F_TIMO flag.
      */
      if ((c_p->flags & (F_INSLPQUEUE | F_TIMO)) == 0) {
        Eterm timeout_value = targ1;
        if (timeout_value == make_small(0)) {

          SET_I((BeamInstr *) I+3);
          Goto(*I);;
        } else if (lb_receive_is_infinity(c_p,timeout_value)) {
          c_p->flags |= F_TIMO;
        } else {
          int tres=!is_small(timeout_value) || signed_val(timeout_value)<0;
          if(!tres) {
            LbProcessStatus lb_timer_status=lb_receive_timer_set(c_p,(Uint)signed_val(timeout_value));
            if(lb_timer_status==LB_PROCESS_LIMIT) goto resource_limit;
            if(lb_timer_status!=LB_PROCESS_READY) goto environment_failure;
          }
          if (tres == 0) {
            /*
            * The timer routiner will set c_p->i to the value in
            * c_p->def_arg_reg[0].  Note that it is safe to use this
            * location because there are no living x registers in
            * a receive statement.
            */
            c_p->receive_timeout_pc=I+3;
          } else { /* Wrong time */

            c_p->freason = LB_EXC_TIMEOUT_VALUE;
            c_p->messages.save=&c_p->messages.first;
            goto find_func_info;
          }
        }
      }
    }
  }
  I += 1;
  goto wait__execute;

  wait__execute:
  {
    SET_I(I+(Sint)I[1]);
    ASSERT(VALID_INSTR(*(const BeamInstr*)c_p->i));; /* L1 */
    SWAPOUT;
    c_p->arity = 0;

    if (!c_p->receive_timed_out) {
      c_p->waiting=1;
    }
    ASSERT(!(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED));

    c_p->current = NULL;
    goto context_switch3;
    ASSERT(!"Fell through 'wait.execute' (-no_next)");
  }

}

{
  Eterm context;
  OpCase(i_bs_start_match3_xtjd):
  {
    context = xb(I[1]);
  }
  goto i_bs_start_match3__execute;

  OpCase(i_bs_start_match3_ytjd):
  {
    context = yb(I[1]);
  }
  goto i_bs_start_match3__execute;

  i_bs_start_match3__execute:
  {
    Eterm tmp_packed1 = I[2];
    Eterm dst = db((tmp_packed1>>BEAM_LOOSE_SHIFT));
    Eterm* dst_ptr = REG_TARGET_PTR(dst);
    Eterm header;
    Uint live;

    live = tb(tmp_packed1&BEAM_LOOSE_MASK);

    if (!is_boxed(context)) {
      ASSERT(VALID_INSTR(*(I + (Sint)I[3])));
      I += (Sint)I[3];;
      Goto(*I);;
    }

    header = *boxed_val(context);

    if (is_bitstring_header(header)) {
      ErlSubBits *sb;
      int reuse = 0;

      if (header == HEADER_SUB_BITS) {
        sb = (ErlSubBits*)bitstring_val(context);
        reuse = erl_sub_bits_is_match_context(sb);
      }

      if (!reuse) {
        do {
          Uint need = ERL_SUB_BITS_SIZE;
          if (ERTS_UNLIKELY((Uint)(E - HTOP) < (need + S_RESERVED))) {
            do {
              //
              // Since a garbage collection is expensive anyway, we can afford
              // to save the instruction counter so that the correct function will
              // be pointed in the crash dump if the garbage collection fails
              // because of insufficient memory.
              //
              SWAPOUT;
              c_p->i = I;
            } while (0);
            reg[live] = context;

            if (!lb_process_collect_live(c_p, need, live+1)) goto memory_failure;
      FCALLS -= (Sint)c_p->last_gc_cost;


            context = reg[live];
            SWAPIN;

          }
          HEAP_SPACE_VERIFIED(ERL_SUB_BITS_SIZE);
        } while (0);

        HEAP_TOP(c_p) = HTOP;
#ifdef DEBUG
        c_p->stop = E;        /* Needed for checking in HeapOnlyAlloc(). */
#endif
        sb = lb_bs_start_match(c_p, context);
        HTOP = HEAP_TOP(c_p);
        HEAP_SPACE_VERIFIED(0);

        dst_ptr = REG_TARGET_PTR(dst);
        context = make_bitstring(sb);
      }

      *dst_ptr = context;
    } else {
      ASSERT(VALID_INSTR(*(I + (Sint)I[3])));
      I += (Sint)I[3];;
      Goto(*I);;
    }
    I += 4;
    ASSERT(VALID_INSTR(*I));
    Goto(*I);
  }

}

{
  ErlSubBits *sb;
  Uint size;
  OpCase(i_bs_ensure_bits_read_xtf):
  {
    Eterm tmp_packed1 = I[1];
    sb = (ErlSubBits*)bitstring_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
    size = tb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto i_bs_read_bits__ensure_bits;

  OpCase(i_bs_ensure_bits_read_ytf):
  {
    Eterm tmp_packed1 = I[1];
    sb = (ErlSubBits*)bitstring_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
    size = tb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto i_bs_read_bits__ensure_bits;

  OpCase(i_bs_read_bits_xt):
  {
    Eterm tmp_packed1 = I[1];
    sb = (ErlSubBits*)bitstring_val(xb(tmp_packed1&BEAM_LOOSE_MASK));
    size = tb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto i_bs_read_bits__execute;

  OpCase(i_bs_read_bits_yt):
  {
    Eterm tmp_packed1 = I[1];
    sb = (ErlSubBits*)bitstring_val(yb(tmp_packed1&BEAM_LOOSE_MASK));
    size = tb((tmp_packed1>>BEAM_LOOSE_SHIFT));
  }
  goto i_bs_read_bits__execute;

  i_bs_read_bits__ensure_bits:
  {
    if (sb->end - sb->start < size) {
      ASSERT(VALID_INSTR(*(I + (Sint)I[2] + 1)));
      I += (Sint)I[2] + 1;;
      Goto(*I);;
    }
  }
  I += 1;
  goto i_bs_read_bits__execute;

  i_bs_read_bits__execute:
  {
    BeamInstr next_pf = BeamCodeAddr(I[2]);
    const byte *byte_ptr;
    Uint bit_offset = sb->start % 8;
    Uint num_bytes_to_read = (size + 7) / 8;
    Uint num_partial = size % 8;

    if ((num_partial == 0 && bit_offset != 0) ||
    (num_partial != 0 && bit_offset > 8 - num_partial)) {
      num_bytes_to_read++;
    }

    bitdata = 0;
    byte_ptr = erl_sub_bits_get_base(sb) + BYTE_OFFSET(sb->start);
    sb->start += size;
    switch (num_bytes_to_read) {
#ifdef ARCH_64
      case 9:
      case 8:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 7:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 6:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 5:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
#else
      case 5:
#endif
      case 4:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 3:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 2:
      bitdata = bitdata << 8 | *byte_ptr++; ERTS_FALLTHROUGH();
      case 1:
      bitdata = bitdata << 8 | *byte_ptr++;
    }

    if (num_bytes_to_read <= sizeof(Uint)) {
      bitdata <<= 8 * (sizeof(Uint) - num_bytes_to_read) + bit_offset;
    } else {
      bitdata <<= bit_offset;
      bitdata = bitdata | ((*byte_ptr << bit_offset) >> 8);
    }
    I += 2;
    ASSERT(VALID_INSTR(next_pf));
    GotoPF(next_pf);
  }

}

{
  Eterm context;
  OpCase(i_bs_ensure_bits_unit_xItf):
  {
    context = xb(I[1]);
  }
  goto i_bs_ensure_bits_unit__execute;

  OpCase(i_bs_ensure_bits_unit_yItf):
  {
    context = yb(I[1]);
  }
  goto i_bs_ensure_bits_unit__execute;

  i_bs_ensure_bits_unit__execute:
  {
    Eterm tmp_packed1 = I[2];
    ErlSubBits *sb = (ErlSubBits*)bitstring_val(context);
    Uint size = Ib(tmp_packed1&BEAM_WIDE_MASK);
    Uint diff;

    if ((diff = sb->end - sb->start) < size) {
      ASSERT(VALID_INSTR(*(I + (Sint)I[3])));
      I += (Sint)I[3];;
      Goto(*I);;
    }
    if ((diff - size) % tb((tmp_packed1>>BEAM_WIDE_SHIFT)) != 0) {
      ASSERT(VALID_INSTR(*(I + (Sint)I[3])));
      I += (Sint)I[3];;
      Goto(*I);;
    }
    I += 4;
    ASSERT(VALID_INSTR(*I));
    Goto(*I);
  }

}

{
  Eterm context;
  OpCase(i_bs_ensure_bits_xIf):
  {
    context = xb(I[1]);
  }
  goto i_bs_ensure_bits__execute;

  OpCase(i_bs_ensure_bits_yIf):
  {
    context = yb(I[1]);
  }
  goto i_bs_ensure_bits__execute;

  i_bs_ensure_bits__execute:
  {
    Eterm tmp_packed1 = I[2];
    ErlSubBits *sb = (ErlSubBits*)bitstring_val(context);
    Uint size = Ib(tmp_packed1&BEAM_WIDE_MASK);
    if (sb->end - sb->start < size) {
      ASSERT(VALID_INSTR(*(I + (fb((tmp_packed1>>BEAM_WIDE_SHIFT))) + 0)));
      I += fb((tmp_packed1>>BEAM_WIDE_SHIFT)) + 0;;
      Goto(*I);;
    }
    I += 3;
    ASSERT(VALID_INSTR(*I));
    Goto(*I);
  }

}

{
  Eterm map;
  OpCase(update_map_assoc_cdtI): { map=I[1]; } goto map_assoc_execute;
  OpCase(update_map_assoc_xdtI): { map=xb(I[1]); } goto map_assoc_execute;
  OpCase(update_map_assoc_ydtI): { map=yb(I[1]); } goto map_assoc_execute;
map_assoc_execute: {
  Eterm packed=I[2],dst=db(packed&BEAM_LOOSE_MASK);
  Uint live=tb(packed>>BEAM_LOOSE_SHIFT),words=I[3];
  if(words&1 || words>2*LB_FLATMAP_MAX_SIZE || live>=MAX_REG) goto malformed_code;
  size_t count=words/2, reserve=0; int is_map=0;
  reg[live]=map;
  LbMapStatus status=lb_map_is_map(reg[live],&is_map); MAP_STATUS(status);
  if(!is_map) { c_p->heap_reserved=0; c_p->freason=LB_EXC_BADMAP; c_p->fvalue=reg[live]; goto find_func_info; }
  if(count) {
    status=lb_map_reservation_words(reg[live],count,0,&reserve); MAP_STATUS(status);
    c_p->last_native_cost=0; SWAPOUT;
    status=lb_map_reserve(c_p,reserve,live+1);
    SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  }
  map=reg[live];
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE],result;
  for(size_t i=0;i<count;++i) { GetSource(I[4+2*i],keys[i]); GetSource(I[5+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_update_assoc(c_p,map,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=4+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}
}

{
  Eterm map;
  OpCase(update_map_exact_xjdtI): { map=xb(I[1]); } goto map_exact_execute;
  OpCase(update_map_exact_yjdtI): { map=yb(I[1]); } goto map_exact_execute;
map_exact_execute: {
  Eterm packed=I[2], ext=I[3], dst=db(packed>>BEAM_WIDE_SHIFT);
  Sint fail=jb(packed&BEAM_WIDE_MASK);
  Uint live=tb(ext&BEAM_WIDE_MASK),words=Ib(ext>>BEAM_WIDE_SHIFT);
  if((words&1) || words>2*LB_FLATMAP_MAX_SIZE || live>=MAX_REG) goto malformed_code;
  size_t count=words/2,reserve=0; int is_map=0;
  reg[live]=map;
  LbMapStatus status=lb_map_is_map(reg[live],&is_map); MAP_STATUS(status);
  if(!is_map) {
    c_p->heap_reserved=0;
    if(fail) { I+=fail; Goto(*I); }
    c_p->freason=LB_EXC_BADMAP; c_p->fvalue=reg[live]; goto find_func_info;
  }
  if(count) {
    status=lb_map_reservation_words(reg[live],count,1,&reserve); MAP_STATUS(status);
    c_p->last_native_cost=0; SWAPOUT;
    status=lb_map_reserve(c_p,reserve,live+1);
    SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  }
  map=reg[live];
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE],result,missing=NIL;
  for(size_t i=0;i<count;++i) { GetSource(I[4+2*i],keys[i]); GetSource(I[5+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_update_exact_missing(c_p,map,keys,values,count,&result,&missing);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0;
  if(status==LB_MAP_MISSING) {
    if(fail) { I+=fail; Goto(*I); }
    c_p->freason=LB_EXC_BADKEY; c_p->fvalue=missing; goto find_func_info;
  }
  MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=4+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}
}
