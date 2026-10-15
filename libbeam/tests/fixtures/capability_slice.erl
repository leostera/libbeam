%% SPDX-License-Identifier: Apache-2.0
%% Copyright 2026 Leandro Ostera <leandro@ostera.io>
-module(capability_slice).
-export([direct/1, dynamic/1, monitored/1, escaped/1, identity/1]).
-export([d_call_on_load_function_1/1, d_delete_module_1/1, d_disconnect_node_1/1, d_display_1/1,
         d_display_string_1/1, d_display_string_2/1, d_dist_ctrl_put_data_2/1,
         d_finish_after_on_load_2/1, d_finish_loading_1/1,
         d_halt_0/1, d_halt_1/1, d_halt_2/1, d_has_prepared_code_on_load_1/1,
         d_load_nif_2/1, d_monitor_node_2/1, d_monitor_node_3/1,
         d_open_port_2/1, d_port_close_1/1, d_port_command_2/1, d_port_command_3/1,
         d_port_connect_2/1, d_port_control_3/1, d_port_set_data_2/1,
         d_prepare_loading_2/1, d_process_display_2/1, d_purge_module_1/1,
         d_set_cookie_2/1, d_setnode_2/1, d_system_flag_2/1,
         d_trace_3/1, d_trace_info_2/1, d_trace_pattern_3/1]).

direct(_) -> erlang:load_nif("must-not-open-a-library", 0).
dynamic(Input) ->
    {Name, Args} = erlang:binary_to_term(Input),
    erlang:apply(erlang, Name, Args).
monitored(_) ->
    {Pid, Ref} = erlang:spawn_monitor(fun() -> direct(<<>>) end),
    receive
        {'DOWN', Ref, process, Pid, _} -> <<"incorrectly caught child denial">>
    end.
escaped(_) ->
    erlang:register(capability_owner, erlang:self()),
    direct(<<>>).
identity(Input) -> Input.

%% Deliberately invalid arguments still hit whole-MFA policy before effects.
d_call_on_load_function_1(_) -> erlang:call_on_load_function([]).
d_delete_module_1(_) -> erlang:delete_module([]).
d_disconnect_node_1(_) -> erlang:disconnect_node([]).
d_display_1(_) -> erlang:display([]).
d_display_string_1(_) -> erlang:display_string([]).
d_display_string_2(_) -> erlang:display_string([], []).
d_dist_ctrl_put_data_2(_) -> erlang:dist_ctrl_put_data([], []).
d_finish_after_on_load_2(_) -> erlang:finish_after_on_load([], []).
d_finish_loading_1(_) -> erlang:finish_loading([]).
d_halt_0(_) -> erlang:halt().
d_halt_1(_) -> erlang:halt([]).
d_halt_2(_) -> erlang:halt([], []).
d_has_prepared_code_on_load_1(_) -> erlang:has_prepared_code_on_load([]).
d_load_nif_2(_) -> erlang:load_nif([], []).
d_monitor_node_2(_) -> erlang:monitor_node([], []).
d_monitor_node_3(_) -> erlang:monitor_node([], [], []).
d_open_port_2(_) -> erlang:open_port([], []).
d_port_close_1(_) -> erlang:port_close([]).
d_port_command_2(_) -> erlang:port_command([], []).
d_port_command_3(_) -> erlang:port_command([], [], []).
d_port_connect_2(_) -> erlang:port_connect([], []).
d_port_control_3(_) -> erlang:port_control([], [], []).
d_port_set_data_2(_) -> erlang:port_set_data([], []).
d_prepare_loading_2(_) -> erlang:prepare_loading([], []).
d_process_display_2(_) -> erlang:process_display([], []).
d_purge_module_1(_) -> erlang:purge_module([]).
d_set_cookie_2(_) -> erlang:set_cookie([], []).
d_setnode_2(_) -> erlang:setnode([], []).
d_system_flag_2(_) -> erlang:system_flag([], []).
d_trace_3(_) -> erlang:trace([], [], []).
d_trace_info_2(_) -> erlang:trace_info([], []).
d_trace_pattern_3(_) -> erlang:trace_pattern([], [], []).
