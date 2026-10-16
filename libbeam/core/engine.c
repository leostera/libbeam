/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Reversible C ownership for the actual interpreter/native-dispatch substrate.
 * No OTP startup, process table, ethread library, tenant TLS or singleton.
 * A joinable worker is started lazily by actual asynchronous task admission.
 */
#define _XOPEN_SOURCE 700
#include "code_internal.h"
#include "executor.h"
#include "fun.h"
#include <locale.h>

static void release_catalog(LbEngine *engine)
{
    lb_executor_shutdown(engine);
    lb_release(engine->domain,(void *)engine->bifs);
    engine->bifs=NULL;
    if(engine->numeric_locale) freelocale((locale_t)engine->numeric_locale);
    engine->numeric_locale=NULL; engine->closed=1;
}
static void release_control(LbEngine *engine)
{
    LbAllocDomain *domain=engine->domain;
    assert(!engine->owner_live && !engine->spaces && !engine->tasks && !engine->worlds);
    release_catalog(engine);
    lb_release(domain,engine);
    if(lb_alloc_domain_destroy(domain)!=LB_ALLOC_OK) abort();
}
LbEngineStatus lb_engine_create_with_limits(const LbSystemAllocator *allocator,const LbResourceLimits *limits,LbEngine **out)
{
    LbAllocDomain *domain=NULL;
    LbEngine *engine;
    BifEntry *catalog;
    void *memory;
    LbAllocStatus status;
    const struct { unsigned id; BifEntry entry; } natives[]={
        {BIF_universaltime_0,{am_erlang,am_universaltime,0,lb_bif_universaltime_0,BIF_KIND_HEAVY}},
        {BIF_get_module_info_1,{am_erlang,am_get_module_info,1,lb_bif_module_info_1,BIF_KIND_HEAVY}},
        {BIF_get_module_info_2,{am_erlang,am_get_module_info,2,lb_bif_module_info_2,BIF_KIND_HEAVY}},
        {BIF_splus_2,{am_erlang,am_Plus,2,lb_bif_plus_2,BIF_KIND_HEAVY}},
        {BIF_integer_to_binary_1,{am_erlang,am_integer_to_binary,1,lb_bif_integer_to_binary_1,BIF_KIND_HEAVY}},
        {BIF_send_2,{am_erlang,am_send,2,lb_bif_send_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+41u,{am_erlang,am_send,3,lb_bif_send_3,BIF_KIND_HEAVY}},
        {BIF_SIZE+42u,{am_erlang,am_yield,0,lb_bif_yield_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+43u,{am_erlang,am_port_info,1,lb_bif_port_info_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+44u,{am_erlang,am_port_info,2,lb_bif_port_info_2,BIF_KIND_HEAVY}},
        {BIF_port_get_data_1,{am_erlang,am_port_get_data,1,lb_bif_port_get_data_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+45u,{am_erlang,am_memory,0,lb_bif_memory_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+46u,{am_erlang,am_memory,1,lb_bif_memory_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+47u,{am_erlang,am_statistics,1,lb_bif_statistics_1,BIF_KIND_HEAVY}},
        {BIF_bump_reductions_1,{am_erlang,am_bump_reductions,1,lb_bif_bump_reductions_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+48u,{am_erlang,am_localtime_to_universaltime,1,lb_bif_localtime_to_universaltime_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+49u,{am_erlang,am_localtime_to_universaltime,2,lb_bif_localtime_to_universaltime_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+50u,{am_erlang,am_universaltime_to_localtime,1,lb_bif_universaltime_to_localtime_1,BIF_KIND_HEAVY}},
        {BIF_posixtime_to_universaltime_1,{am_erlang,am_posixtime_to_universaltime,1,lb_bif_posixtime_to_universaltime_1,BIF_KIND_HEAVY}},
        {BIF_universaltime_to_posixtime_1,{am_erlang,am_universaltime_to_posixtime,1,lb_bif_universaltime_to_posixtime_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+51u,{am_erlang,am_spawn_request,5,lb_bif_spawn_request_5,BIF_KIND_HEAVY}},
        {BIF_SIZE+52u,{am_erlang,am_processes_iterator,0,lb_bif_processes_iterator_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+53u,{am_erlang,am_processes_next,1,lb_bif_processes_next_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+54u,{am_erlang,am_hibernate,0,lb_bif_hibernate_0,BIF_KIND_HEAVY}},
        {BIF_spawn_request_abandon_1,{am_erlang,am_spawn_request_abandon,1,lb_bif_spawn_request_abandon_1,BIF_KIND_HEAVY}},
        {BIF_match_spec_test_3,{am_erlang,am_match_spec_test,3,lb_bif_match_spec_test_3,BIF_KIND_HEAVY}},
        {BIF_dt_spread_tag_1,{am_erlang,am_dt_spread_tag,1,lb_bif_dt_spread_tag_1,BIF_KIND_HEAVY}},
        {BIF_dt_restore_tag_1,{am_erlang,am_dt_restore_tag,1,lb_bif_dt_restore_tag_1,BIF_KIND_HEAVY}},
        {BIF_is_builtin_3,{am_erlang,am_is_builtin,3,lb_bif_is_builtin_3,BIF_KIND_HEAVY}},
        {BIF_self_0,{am_erlang,am_self,0,lb_bif_self_0,BIF_KIND_HEAVY}},
        {BIF_pid_to_list_1,{am_erlang,am_pid_to_list,1,lb_bif_pid_to_list_1,BIF_KIND_HEAVY}},
        {BIF_list_to_pid_1,{am_erlang,am_list_to_pid,1,lb_bif_list_to_pid_1,BIF_KIND_HEAVY}},
        {BIF_ref_to_list_1,{am_erlang,am_ref_to_list,1,lb_bif_ref_to_list_1,BIF_KIND_HEAVY}},
        {BIF_list_to_ref_1,{am_erlang,am_list_to_ref,1,lb_bif_list_to_ref_1,BIF_KIND_HEAVY}},
        {BIF_port_to_list_1,{am_erlang,am_port_to_list,1,lb_bif_port_to_list_1,BIF_KIND_HEAVY}},
        {BIF_list_to_port_1,{am_erlang,am_list_to_port,1,lb_bif_list_to_port_1,BIF_KIND_HEAVY}},
        {BIF_phash_2,{am_erlang,am_phash,2,lb_bif_phash_2,BIF_KIND_HEAVY}},
        {BIF_phash2_1,{am_erlang,am_phash2,1,lb_bif_phash2_1,BIF_KIND_HEAVY}},
        {BIF_phash2_2,{am_erlang,am_phash2,2,lb_bif_phash2_2,BIF_KIND_HEAVY}},
        {BIF_group_leader_0,{am_erlang,am_group_leader,0,lb_bif_group_leader_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+21u,{am_erlang,am_group_leader,2,lb_bif_group_leader_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+22u,{am_erlang,am_spawn_monitor,3,lb_bif_spawn_monitor_3,BIF_KIND_HEAVY}},
        {BIF_SIZE+19u,{am_erlang,am_garbage_collect,0,lb_bif_garbage_collect_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+20u,{am_erlang,am_garbage_collect,1,lb_bif_garbage_collect_1,BIF_KIND_HEAVY}},
        {BIF_node_0,{am_erlang,am_node,0,lb_bif_node_0,BIF_KIND_HEAVY}},
        {BIF_node_1,{am_erlang,am_node,1,lb_bif_node_1,BIF_KIND_GUARD}},
        {BIF_nodes_0,{am_erlang,am_nodes,0,lb_bif_nodes_0,BIF_KIND_HEAVY}},
        {BIF_nodes_1,{am_erlang,am_nodes,1,lb_bif_nodes_1,BIF_KIND_HEAVY}},
        {BIF_nodes_2,{am_erlang,am_nodes,2,lb_bif_nodes_2,BIF_KIND_HEAVY}},
        {BIF_ports_0,{am_erlang,am_ports,0,lb_bif_ports_0,BIF_KIND_HEAVY}},
        {BIF_make_ref_0,{am_erlang,am_make_ref,0,lb_bif_make_ref_0,BIF_KIND_HEAVY}},
        {BIF_unique_integer_0,{am_erlang,am_unique_integer,0,lb_bif_unique_integer_0,BIF_KIND_HEAVY}},
        {BIF_unique_integer_1,{am_erlang,am_unique_integer,1,lb_bif_unique_integer_1,BIF_KIND_HEAVY}},
        {BIF_is_function_1,{am_erlang,am_is_function,1,lb_bif_is_function_1,BIF_KIND_GUARD}},
        {BIF_is_function_2,{am_erlang,am_is_function,2,lb_bif_is_function_2,BIF_KIND_GUARD}},
        {BIF_fun_info_2,{am_erlang,am_fun_info,2,lb_bif_fun_info_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+28u,{am_erlang,am_fun_info,1,lb_bif_fun_info_1,BIF_KIND_HEAVY}},
        {BIF_make_fun_3,{am_erlang,am_make_fun,3,lb_bif_make_fun_3,BIF_KIND_HEAVY}},
        {BIF_fun_info_mfa_1,{am_erlang,am_fun_info_mfa,1,lb_bif_fun_info_mfa_1,BIF_KIND_HEAVY}},
        {BIF_fun_to_list_1,{am_erlang,am_fun_to_list,1,lb_bif_fun_to_list_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+32u,{am_erlang,am_spawn,2,lb_bif_spawn_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+34u,{am_erlang,am_spawn,4,lb_bif_spawn_4,BIF_KIND_HEAVY}},
        {BIF_SIZE+35u,{am_erlang,am_spawn_opt,5,lb_bif_spawn_opt_5,BIF_KIND_HEAVY}},
        {BIF_SIZE+36u,{am_erlang,am_spawn_link,2,lb_bif_spawn_link_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+37u,{am_erlang,am_spawn_link,4,lb_bif_spawn_link_4,BIF_KIND_HEAVY}},
        {BIF_SIZE+38u,{am_erlang,am_spawn_monitor,2,lb_bif_spawn_monitor_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+39u,{am_erlang,am_spawn_monitor,4,lb_bif_spawn_monitor_4,BIF_KIND_HEAVY}},
        {BIF_SIZE+33u,{am_erlang,am_spawn_opt,3,lb_bif_spawn_opt_3,BIF_KIND_HEAVY}},
        {BIF_spawn_3,{am_erlang,am_spawn,3,lb_bif_spawn_3,BIF_KIND_HEAVY}},
        {BIF_error_1,{am_erlang,am_error,1,lb_bif_error_1,BIF_KIND_HEAVY}},
        {BIF_exit_1,{am_erlang,am_exit,1,lb_bif_exit_1,BIF_KIND_HEAVY}},
        {BIF_nif_error_1,{am_erlang,am_nif_error,1,lb_bif_nif_error_1,BIF_KIND_HEAVY}},
        {BIF_register_2,{am_erlang,am_register,2,lb_bif_register_2,BIF_KIND_HEAVY}},
        {BIF_registered_0,{am_erlang,am_registered,0,lb_bif_registered_0,BIF_KIND_HEAVY}},
        {BIF_whereis_1,{am_erlang,am_whereis,1,lb_bif_whereis_1,BIF_KIND_HEAVY}},
        {BIF_unregister_1,{am_erlang,am_unregister,1,lb_bif_unregister_1,BIF_KIND_HEAVY}},
        {BIF_error_3,{am_erlang,am_error,3,lb_bif_error_3,BIF_KIND_HEAVY}},
        {BIF_SIZE+25u,{am_erlang,am_spawn_opt,2,lb_bif_spawn_opt_2,BIF_KIND_HEAVY}},
        {BIF_spawn_opt_4,{am_erlang,am_spawn_opt,4,lb_bif_spawn_opt_4,BIF_KIND_HEAVY}},
        {BIF_monitor_2,{am_erlang,am_monitor,2,lb_bif_monitor_2,BIF_KIND_HEAVY}},
        {BIF_monitor_3,{am_erlang,am_monitor,3,lb_bif_monitor_3,BIF_KIND_HEAVY}},
        {BIF_demonitor_1,{am_erlang,am_demonitor,1,lb_bif_demonitor_1,BIF_KIND_HEAVY}},
        {BIF_demonitor_2,{am_erlang,am_demonitor,2,lb_bif_demonitor_2,BIF_KIND_HEAVY}},
        {BIF_exit_2,{am_erlang,am_exit,2,lb_bif_exit_2,BIF_KIND_HEAVY}},
        {BIF_link_1,{am_erlang,am_link,1,lb_bif_link_1,BIF_KIND_HEAVY}},
        {BIF_spawn_link_3,{am_erlang,am_spawn_link,3,lb_bif_spawn_link_3,BIF_KIND_HEAVY}},
        {BIF_unlink_1,{am_erlang,am_unlink,1,lb_bif_unlink_1,BIF_KIND_HEAVY}},
        {BIF_process_flag_2,{am_erlang,am_process_flag,2,lb_bif_process_flag_2,BIF_KIND_HEAVY}},
        {BIF_iolist_to_binary_1,{am_erlang,am_iolist_to_binary,1,lb_bif_iolist_to_binary_1,BIF_KIND_HEAVY}},
        {BIF_iolist_to_iovec_1,{am_erlang,am_iolist_to_iovec,1,lb_bif_iolist_to_iovec_1,BIF_KIND_HEAVY}},
        {BIF_list_to_bitstring_1,{am_erlang,am_list_to_bitstring,1,lb_bif_list_to_bitstring_1,BIF_KIND_HEAVY}},
        {BIF_list_to_binary_1,{am_erlang,am_list_to_binary,1,lb_bif_list_to_binary_1,BIF_KIND_HEAVY}},
        {BIF_md5_1,{am_erlang,am_md5,1,lb_bif_md5_1,BIF_KIND_HEAVY}},
        {BIF_md5_init_0,{am_erlang,am_md5_init,0,lb_bif_md5_init_0,BIF_KIND_HEAVY}},
        {BIF_md5_update_2,{am_erlang,am_md5_update,2,lb_bif_md5_update_2,BIF_KIND_HEAVY}},
        {BIF_md5_final_1,{am_erlang,am_md5_final,1,lb_bif_md5_final_1,BIF_KIND_HEAVY}},
        {BIF_decode_packet_3,{am_erlang,am_decode_packet,3,lb_bif_decode_packet_3,BIF_KIND_HEAVY}},
        {BIF_iolist_size_1,{am_erlang,am_iolist_size,1,lb_bif_iolist_size_1,BIF_KIND_HEAVY}},
        {BIF_binary_to_list_1,{am_erlang,am_binary_to_list,1,lb_bif_binary_to_list_1,BIF_KIND_HEAVY}},
        {BIF_bitstring_to_list_1,{am_erlang,am_bitstring_to_list,1,lb_bif_bitstring_to_list_1,BIF_KIND_HEAVY}},
        {BIF_binary_to_list_3,{am_erlang,am_binary_to_list,3,lb_bif_binary_to_list_3,BIF_KIND_HEAVY}},
        {BIF_byte_size_1,{am_erlang,am_byte_size,1,lb_bif_byte_size_1,BIF_KIND_GUARD}},
        {BIF_bit_size_1,{am_erlang,am_bit_size,1,lb_bif_bit_size_1,BIF_KIND_GUARD}},
        {BIF_throw_1,{am_erlang,am_throw,1,lb_bif_throw_1,BIF_KIND_HEAVY}},
        {BIF_sminus_2,{am_erlang,am_Minus,2,lb_bif_minus_2,BIF_KIND_HEAVY}},
        {BIF_binary_part_3,{am_erlang,am_binary_part,3,lb_bif_binary_part_3,BIF_KIND_HEAVY}},
        {BIF_bsr_2,{am_erlang,am_bsr,2,lb_bif_bsr_2,BIF_KIND_HEAVY}},
        {BIF_band_2,{am_erlang,am_band,2,lb_bif_band_2,BIF_KIND_HEAVY}},
        {BIF_bsl_2,{am_erlang,am_bsl,2,lb_bif_bsl_2,BIF_KIND_HEAVY}},
        /* erlang:binary_to_integer/1 is an ordinary preloaded wrapper in OTP,
         * but its bounded integer conversion is native here under that exact MFA. */
        {BIF_SIZE,{am_erlang,am_binary_to_integer,1,lb_bif_binary_to_integer_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+23u,{am_erlang,am_binary_to_integer,2,lb_bif_binary_to_integer_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+24u,{am_erlang,am_convert_time_unit,3,lb_bif_convert_time_unit_3,BIF_KIND_HEAVY}},
        {BIF_system_info_1,{am_erlang,am_system_info,1,lb_bif_system_info_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+29u,{am_erlang,am_is_alive,0,lb_bif_is_alive_0,BIF_KIND_HEAVY}},
        {BIF_SIZE+31u,{am_erlang,am_get_cookie,0,lb_bif_get_cookie_0,BIF_KIND_HEAVY}},
        {BIF_localtime_0,{am_erlang,am_localtime,0,lb_bif_localtime_0,BIF_KIND_HEAVY}},
        {BIF_timestamp_0,{am_erlang,am_timestamp,0,lb_bif_timestamp_0,BIF_KIND_HEAVY}},
        {BIF_monotonic_time_0,{am_erlang,am_monotonic_time,0,lb_bif_monotonic_time_0,BIF_KIND_HEAVY}},
        {BIF_monotonic_time_1,{am_erlang,am_monotonic_time,1,lb_bif_monotonic_time_1,BIF_KIND_HEAVY}},
        {BIF_system_time_0,{am_erlang,am_system_time,0,lb_bif_system_time_0,BIF_KIND_HEAVY}},
        {BIF_system_time_1,{am_erlang,am_system_time,1,lb_bif_system_time_1,BIF_KIND_HEAVY}},
        {BIF_time_offset_0,{am_erlang,am_time_offset,0,lb_bif_time_offset_0,BIF_KIND_HEAVY}},
        {BIF_time_offset_1,{am_erlang,am_time_offset,1,lb_bif_time_offset_1,BIF_KIND_HEAVY}},
        {BIF_send_after_3,{am_erlang,am_send_after,3,lb_bif_send_after_3,BIF_KIND_HEAVY}},
        {BIF_start_timer_3,{am_erlang,am_start_timer,3,lb_bif_start_timer_3,BIF_KIND_HEAVY}},
        {BIF_send_after_4,{am_erlang,am_send_after,4,lb_bif_send_after_4,BIF_KIND_HEAVY}},
        {BIF_start_timer_4,{am_erlang,am_start_timer,4,lb_bif_start_timer_4,BIF_KIND_HEAVY}},
        {BIF_cancel_timer_1,{am_erlang,am_cancel_timer,1,lb_bif_cancel_timer_1,BIF_KIND_HEAVY}},
        {BIF_cancel_timer_2,{am_erlang,am_cancel_timer,2,lb_bif_cancel_timer_2,BIF_KIND_HEAVY}},
        {BIF_read_timer_1,{am_erlang,am_read_timer,1,lb_bif_read_timer_1,BIF_KIND_HEAVY}},
        {BIF_alias_1,{am_erlang,am_alias,1,lb_bif_alias_1,BIF_KIND_HEAVY}},
        {BIF_unalias_1,{am_erlang,am_unalias,1,lb_bif_unalias_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+26u,{am_erlang,am_alias,0,lb_bif_alias_0,BIF_KIND_HEAVY}},
        {BIF_binary_to_float_1,{am_erlang,am_binary_to_float,1,lb_bif_binary_to_float_1,BIF_KIND_HEAVY}},
        {BIF_list_to_float_1,{am_erlang,am_list_to_float,1,lb_bif_list_to_float_1,BIF_KIND_HEAVY}},
        {BIF_binary_to_atom_2,{am_erlang,am_binary_to_atom,2,lb_bif_binary_to_atom_2,BIF_KIND_HEAVY}},
        {BIF_binary_to_existing_atom_2,{am_erlang,am_binary_to_existing_atom,2,lb_bif_binary_to_existing_atom_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+1u,{am_erlang,am_binary_to_atom,1,lb_bif_binary_to_atom_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+2u,{am_erlang,am_binary_to_existing_atom,1,lb_bif_binary_to_existing_atom_1,BIF_KIND_HEAVY}},
        {BIF_ebif_plusplus_2,{am_erlang,am_PlusPlus,2,lb_bif_plusplus_2,BIF_KIND_HEAVY}},
        {BIF_atom_to_binary_2,{am_erlang,am_atom_to_binary,2,lb_bif_atom_to_binary_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+3u,{am_erlang,am_atom_to_binary,1,lb_bif_atom_to_binary_1,BIF_KIND_HEAVY}},
        {BIF_integer_to_binary_2,{am_erlang,am_integer_to_binary,2,lb_bif_integer_to_binary_2,BIF_KIND_HEAVY}},
        {BIF_integer_to_list_1,{am_erlang,am_integer_to_list,1,lb_bif_integer_to_list_1,BIF_KIND_HEAVY}},
        {BIF_integer_to_list_2,{am_erlang,am_integer_to_list,2,lb_bif_integer_to_list_2,BIF_KIND_HEAVY}},
        {BIF_bor_2,{am_erlang,am_bor,2,lb_bif_bor_2,BIF_KIND_HEAVY}},
        {BIF_bxor_2,{am_erlang,am_bxor,2,lb_bif_bxor_2,BIF_KIND_HEAVY}},
        {BIF_bnot_1,{am_erlang,am_bnot,1,lb_bif_bnot_1,BIF_KIND_HEAVY}},
        {BIF_float_to_binary_2,{am_erlang,am_float_to_binary,2,lb_bif_float_to_binary_2,BIF_KIND_HEAVY}},
        {BIF_float_to_list_1,{am_erlang,am_float_to_list,1,lb_bif_float_to_list_1,BIF_KIND_HEAVY}},
        {BIF_float_to_list_2,{am_erlang,am_float_to_list,2,lb_bif_float_to_list_2,BIF_KIND_HEAVY}},
        {BIF_ebif_minusminus_2,{am_erlang,am_MinusMinus,2,lb_bif_minusminus_2,BIF_KIND_HEAVY}},
        {BIF_split_binary_2,{am_erlang,am_split_binary,2,lb_bif_split_binary_2,BIF_KIND_HEAVY}},
        {BIF_seq_2,{am_erlang,am_Eq,2,lb_bif_seq_2,BIF_KIND_GUARD}},
        {BIF_sneq_2,{am_erlang,am_Neq,2,lb_bif_sneq_2,BIF_KIND_GUARD}},
        {BIF_element_2,{am_erlang,am_element,2,lb_bif_element_2,BIF_KIND_GUARD}},
        {BIF_length_1,{am_erlang,am_length,1,lb_bif_length_1,BIF_KIND_GUARD}},
        {BIF_sge_2,{am_erlang,am_Ge,2,lb_bif_sge_2,BIF_KIND_GUARD}},
        {BIF_sgt_2,{am_erlang,am_Gt,2,lb_bif_sgt_2,BIF_KIND_GUARD}},
        {BIF_sle_2,{am_erlang,am_Le,2,lb_bif_sle_2,BIF_KIND_GUARD}},
        {BIF_slt_2,{am_erlang,am_Lt,2,lb_bif_slt_2,BIF_KIND_GUARD}},
        {BIF_seqeq_2,{am_erlang,am_Eqeq,2,lb_bif_seqeq_2,BIF_KIND_GUARD}},
        {BIF_sneqeq_2,{am_erlang,am_Neqeq,2,lb_bif_sneqeq_2,BIF_KIND_GUARD}},
        {BIF_map_get_2,{am_erlang,am_map_get,2,lb_bif_map_get_2,BIF_KIND_GUARD}},
        {BIF_raise_3,{am_erlang,am_raise,3,lb_bif_raise_3,BIF_KIND_HEAVY}},
        {BIF_max_2,{am_erlang,am_max,2,lb_bif_max_2,BIF_KIND_GUARD}},
        {BIF_setelement_3,{am_erlang,am_setelement,3,lb_bif_setelement_3,BIF_KIND_HEAVY}},
        {BIF_insert_element_3,{am_erlang,am_insert_element,3,lb_bif_insert_element_3,BIF_KIND_HEAVY}},
        {BIF_stimes_2,{am_erlang,am_Times,2,lb_bif_times_2,BIF_KIND_HEAVY}},
        {BIF_div_2,{am_erlang,am_Div,2,lb_bif_div_2,BIF_KIND_HEAVY}},
        {BIF_intdiv_2,{am_erlang,am_div,2,lb_bif_intdiv_2,BIF_KIND_HEAVY}},
        {BIF_abs_1,{am_erlang,am_abs,1,lb_bif_abs_1,BIF_KIND_HEAVY}},
        {BIF_sminus_1,{am_erlang,am_Minus,1,lb_bif_minus_1,BIF_KIND_HEAVY}},
        {BIF_splus_1,{am_erlang,am_Plus,1,lb_bif_plus_1,BIF_KIND_GUARD}},
        {BIF_rem_2,{am_erlang,am_rem,2,lb_bif_rem_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+4u,{am_erlang,am_list_to_integer,1,lb_bif_list_to_integer_1,BIF_KIND_HEAVY}},
        {BIF_erts_internal_list_to_integer_2,{am_erts_internal,am_list_to_integer,2,lb_bif_list_to_integer_2,BIF_KIND_HEAVY}},
        {BIF_SIZE+27u,{am_erlang,am_list_to_integer,2,lb_bif_list_to_integer_2,BIF_KIND_HEAVY}},
        {BIF_min_2,{am_erlang,am_min,2,lb_bif_min_2,BIF_KIND_GUARD}},
        {BIF_is_map_key_2,{am_erlang,am_is_map_key,2,lb_bif_is_map_key_2,BIF_KIND_GUARD}},
        {BIF_map_size_1,{am_erlang,am_map_size,1,lb_bif_map_size_1,BIF_KIND_GUARD}},
        {BIF_is_integer_1,{am_erlang,am_is_integer,1,lb_bif_is_integer_1,BIF_KIND_GUARD}},
        {BIF_is_integer_3,{am_erlang,am_is_integer,3,lb_bif_is_integer_3,BIF_KIND_GUARD}},
        {BIF_is_record_2,{am_erlang,am_is_record,2,lb_bif_is_record_2,BIF_KIND_GUARD}},
        {BIF_is_record_3,{am_erlang,am_is_record,3,lb_bif_is_record_3,BIF_KIND_GUARD}},
        {BIF_hd_1,{am_erlang,am_hd,1,lb_bif_hd_1,BIF_KIND_GUARD}},
        {BIF_tl_1,{am_erlang,am_tl,1,lb_bif_tl_1,BIF_KIND_GUARD}},
        {BIF_is_list_1,{am_erlang,am_is_list,1,lb_bif_is_list_1,BIF_KIND_GUARD}},
        {BIF_not_1,{am_erlang,am_not,1,lb_bif_not_1,BIF_KIND_GUARD}},
        {BIF_and_2,{am_erlang,am_and,2,lb_bif_and_2,BIF_KIND_GUARD}},
        {BIF_or_2,{am_erlang,am_or,2,lb_bif_or_2,BIF_KIND_GUARD}},
        {BIF_xor_2,{am_erlang,am_xor,2,lb_bif_xor_2,BIF_KIND_GUARD}},
        {BIF_is_atom_1,{am_erlang,am_is_atom,1,lb_bif_is_atom_1,BIF_KIND_GUARD}},
        {BIF_list_to_atom_1,{am_erlang,am_list_to_atom,1,lb_bif_list_to_atom_1,BIF_KIND_HEAVY}},
        {BIF_list_to_existing_atom_1,{am_erlang,am_list_to_existing_atom,1,lb_bif_list_to_existing_atom_1,BIF_KIND_HEAVY}},
        {BIF_atom_to_list_1,{am_erlang,am_atom_to_list,1,lb_bif_atom_to_list_1,BIF_KIND_HEAVY}},
        {BIF_is_binary_1,{am_erlang,am_is_binary,1,lb_bif_is_binary_1,BIF_KIND_GUARD}},
        {BIF_function_exported_3,{am_erlang,am_function_exported,3,lb_bif_function_exported_3,BIF_KIND_HEAVY}},
        {BIF_process_info_1,{am_erlang,am_process_info,1,lb_bif_process_info_1,BIF_KIND_HEAVY}},
        {BIF_processes_0,{am_erlang,am_processes,0,lb_bif_processes_0,BIF_KIND_HEAVY}},
        {BIF_process_info_2,{am_erlang,am_process_info,2,lb_bif_process_info_2,BIF_KIND_HEAVY}},
        {BIF_is_process_alive_1,{am_erlang,am_is_process_alive,1,lb_bif_is_process_alive_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+40u,{am_erlang,am_process_flag,3,lb_bif_process_flag_3,BIF_KIND_HEAVY}},
        {BIF_binary_to_term_1,{am_erlang,am_binary_to_term,1,lb_bif_binary_to_term_1,BIF_KIND_HEAVY}},
        {BIF_external_size_1,{am_erlang,am_external_size,1,lb_bif_external_size_1,BIF_KIND_HEAVY}},
        {BIF_term_to_binary_1,{am_erlang,am_term_to_binary,1,lb_bif_term_to_binary_1,BIF_KIND_HEAVY}},
        {BIF_term_to_binary_2,{am_erlang,am_term_to_binary,2,lb_bif_term_to_binary_2,BIF_KIND_HEAVY}},
        {BIF_loaded_0,{am_erlang,am_loaded,0,lb_bif_loaded_0,BIF_KIND_HEAVY}},
        {BIF_pre_loaded_0,{am_erlang,am_pre_loaded,0,lb_bif_pre_loaded_0,BIF_KIND_HEAVY}},
        {BIF_module_loaded_1,{am_erlang,am_module_loaded,1,lb_bif_module_loaded_1,BIF_KIND_HEAVY}},
        {BIF_SIZE+18u,{am_erlang,am_load_module,2,lb_bif_load_module_2,BIF_KIND_HEAVY}},
        {BIF_get_0,{am_erlang,am_get,0,lb_bif_get_0,BIF_KIND_HEAVY}},
        {BIF_get_keys_0,{am_erlang,am_get_keys,0,lb_bif_get_keys_0,BIF_KIND_HEAVY}},
        {BIF_get_keys_1,{am_erlang,am_get_keys,1,lb_bif_get_keys_1,BIF_KIND_HEAVY}},
        {BIF_hibernate_3,{am_erlang,am_hibernate,3,lb_bif_hibernate_3,BIF_KIND_HEAVY}},
        {BIF_get_1,{am_erlang,am_get,1,lb_bif_get_1,BIF_KIND_HEAVY}},
        {BIF_put_2,{am_erlang,am_put,2,lb_bif_put_2,BIF_KIND_HEAVY}},
        {BIF_erase_0,{am_erlang,am_erase,0,lb_bif_erase_0,BIF_KIND_HEAVY}},
        {BIF_erase_1,{am_erlang,am_erase,1,lb_bif_erase_1,BIF_KIND_HEAVY}},
        {BIF_error_2,{am_erlang,am_error,2,lb_bif_error_2,BIF_KIND_HEAVY}},
        {BIF_make_tuple_2,{am_erlang,am_make_tuple,2,lb_bif_make_tuple_2,BIF_KIND_HEAVY}},
        {BIF_tuple_to_list_1,{am_erlang,am_tuple_to_list,1,lb_bif_tuple_to_list_1,BIF_KIND_HEAVY}},
        {BIF_list_to_tuple_1,{am_erlang,am_list_to_tuple,1,lb_bif_list_to_tuple_1,BIF_KIND_HEAVY}},
        {BIF_floor_1,{am_erlang,am_floor,1,lb_bif_floor_1,BIF_KIND_HEAVY}},
        {BIF_ceil_1,{am_erlang,am_ceil,1,lb_bif_ceil_1,BIF_KIND_HEAVY}},
        {BIF_trunc_1,{am_erlang,am_trunc,1,lb_bif_trunc_1,BIF_KIND_HEAVY}},
        {BIF_round_1,{am_erlang,am_round,1,lb_bif_round_1,BIF_KIND_HEAVY}},
        {BIF_float_1,{am_erlang,am_float,1,lb_bif_float_1,BIF_KIND_HEAVY}},
        {BIF_re_import_1,{am_re,am_import,1,lb_bif_re_import_1,BIF_KIND_HEAVY}},
        {BIF_re_version_0,{am_re,am_version,0,lb_bif_re_version_0,BIF_KIND_HEAVY}},
        {BIF_re_run_3,{am_re,am_run,3,lb_bif_re_run_3,BIF_KIND_HEAVY}},
        {BIF_binary_match_2,{am_binary,am_match,2,lb_bif_binary_match_2,BIF_KIND_HEAVY}},
        {BIF_binary_split_2,{am_binary,am_split,2,lb_bif_binary_split_2,BIF_KIND_HEAVY}},
        {BIF_maps_from_list_1,{am_maps,am_from_list,1,lb_bif_maps_from_list_1,BIF_KIND_HEAVY}},
        {BIF_maps_merge_2,{am_maps,am_merge,2,lb_bif_maps_merge_2,BIF_KIND_HEAVY}},
        {BIF_erts_internal_map_next_3,{am_erts_internal,am_map_next,3,lb_bif_map_next_3,BIF_KIND_HEAVY}},
        {LB_BIF_LISTS_REVERSE_2,{NIL,NIL,2,lb_bif_lists_reverse_2,BIF_KIND_HEAVY}},
#define LB_TYPE_PREDICATE(name, predicate) {BIF_##name##_1,{am_erlang,am_##name,1,lb_bif_##name##_1,BIF_KIND_GUARD}},
#include "type_predicates.def"
#undef LB_TYPE_PREDICATE
        {BIF_tuple_size_1,{am_erlang,am_tuple_size,1,lb_bif_tuple_size_1,BIF_KIND_GUARD}},
#define LB_DENIAL(id, name, arity, category) {id,{am_erlang,am_##name,arity,lb_bif_deny_##name##_##arity,BIF_KIND_HEAVY}},
#include "capability_denials.def"
#undef LB_DENIAL
    };
    size_t i;
    _Static_assert(sizeof(natives)/sizeof(natives[0])==LB_NATIVE_COUNT,"native catalog size");
    _Static_assert(BIF_get_module_info_1<BIF_SIZE && BIF_get_module_info_2<BIF_SIZE && BIF_splus_2<BIF_SIZE && BIF_integer_to_binary_1<BIF_SIZE,"native identities");
    _Static_assert(BIF_SIZE<UINT_MAX-LB_NATIVE_EXTENSION_COUNT,"native extension identities");
    if(!out) return LB_ENGINE_INVALID;
    *out=NULL;
    LbResourceLimits selected=limits?*limits:lb_resource_defaults();
    if(!lb_resource_limits_valid(selected)) return LB_ENGINE_INVALID;
    status=lb_alloc_domain_create(allocator,&domain);
    if(status!=LB_ALLOC_OK) return status==LB_ALLOC_INVALID?LB_ENGINE_INVALID:LB_ENGINE_NO_MEMORY;
    if(lb_alloc_domain_allocate(domain,sizeof(*engine),&memory)!=LB_ALLOC_OK) {
        if(lb_alloc_domain_destroy(domain)!=LB_ALLOC_OK) abort();
        return LB_ENGINE_NO_MEMORY;
    }
    engine=memory; memset(engine,0,sizeof(*engine)); engine->domain=domain; engine->limits=selected;
    engine->empty_tuple[0]=_make_header(0,_TAG_HEADER_ARITYVAL);
    engine->empty_tuple[1]=THE_NON_VALUE;
    engine->numeric_locale=(void *)newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
    if(!engine->numeric_locale) { lb_release(domain,engine); if(lb_alloc_domain_destroy(domain)!=LB_ALLOC_OK) abort(); return LB_ENGINE_NO_MEMORY; }
    if(lb_alloc_domain_allocate(domain,(BIF_SIZE+LB_NATIVE_EXTENSION_COUNT)*sizeof(BifEntry),&memory)!=LB_ALLOC_OK) {
        release_control(engine); return LB_ENGINE_NO_MEMORY;
    }
    catalog=memory; engine->bifs=catalog; memset(catalog,0,(BIF_SIZE+LB_NATIVE_EXTENSION_COUNT)*sizeof(*catalog));
    for(i=0;i<LB_NATIVE_COUNT;++i) {
        /* Implementations may collect; explicit denials never perform their
         * prohibited effects. Numeric identities alone are not permissions. */
        assert(natives[i].id<BIF_SIZE+LB_NATIVE_EXTENSION_COUNT && !catalog[natives[i].id].f);
        engine->native_ids[i]=natives[i].id;
        catalog[natives[i].id]=natives[i].entry;
    }
    if(lb_executor_init(engine)!=LB_ENGINE_OK) {
        release_control(engine); return LB_ENGINE_NO_MEMORY;
    }
    engine->owner_live=1; *out=engine; return LB_ENGINE_OK;
}
int lb_engine_is_open(const LbEngine *engine)
{
    return engine && engine->owner_live && !engine->closed;
}
LbEngineStatus lb_engine_create(const LbSystemAllocator *allocator,LbEngine **out)
{ return lb_engine_create_with_limits(allocator,NULL,out); }
LbEngineStatus lb_engine_shutdown(LbEngine *engine)
{
    if(!engine) return LB_ENGINE_INVALID;
    if(!lb_engine_is_open(engine)) return LB_ENGINE_CLOSED;
    if(engine->control_borrow) return LB_ENGINE_BUSY;
    lb_engine_enter(engine);
    if(engine->spaces || engine->tasks || engine->worlds) {
        lb_engine_leave(engine); return LB_ENGINE_BUSY;
    }
    engine->closed=1; /* close admission before dropping serialization to join */
    lb_engine_leave(engine); release_catalog(engine); return LB_ENGINE_OK;
}
void lb_engine_release(LbEngine *engine)
{
    if(!engine) return;
    if(!engine->owner_live) abort(); /* duplicate consumption of an internal handle */
    if(engine->control_borrow) { engine->owner_live=0; return; }
    if(engine->sync_ready) {
        lb_engine_enter(engine); engine->owner_live=0; lb_engine_leave(engine);
    } else {
        engine->owner_live=0; lb_engine_release_detached(engine);
    }
}
int lb_engine_is_detached(const LbEngine *engine)
{
    return !engine->owner_live && !engine->spaces && !engine->tasks && !engine->worlds && !engine->control_borrow;
}
void lb_engine_release_detached(LbEngine *engine)
{
    if(!lb_engine_is_detached(engine)) abort();
    release_control(engine);
}
void lb_engine_space_published(LbEngine *engine)
{
    if(!lb_engine_is_open(engine) || engine->spaces==SIZE_MAX) abort();
    ++engine->spaces;
}
void lb_engine_space_released(LbEngine *engine)
{
    if(!engine->spaces) abort();
    --engine->spaces;
    /* The worker now retires world code spaces and orphan task controls. Only
     * host leave may decide final disposal with a live worker, under its mutex,
     * then unlock and join. No unlocked ownership reads and no worker self-join. */
    if(!engine->worker_started && !engine->control_borrow && lb_engine_is_detached(engine))
        lb_engine_release_detached(engine);
}
