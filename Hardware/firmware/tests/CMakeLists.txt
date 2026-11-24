cmake_minimum_required(VERSION 3.13)
project(pitrac_host_tests C)

# Native host tests. NOT an RP2350 target: these compile real firmware sources
# (power_fsm.c, strobe_plan.c, strobe_live.c, service.c, telemetry.c,
# config_store.c, link_frame.c, link_proto.c, sha256.c, fw_update.c, cam_plan.c)
# against small mocks and never talk to the board. cam_pio_test also runs the
# real PIO programs (cam_handshake, strobe_gate, strobe_burst) in pio_sim.c.
include(CTest)

function(pitrac_host_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE mocks ../src)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

pitrac_host_test(power_fsm_test power_fsm_test.c ../src/power_fsm.c ../src/telemetry.c)
pitrac_host_test(telemetry_test telemetry_test.c ../src/telemetry.c)
pitrac_host_test(config_store_test config_store_test.c ../src/config_store.c ../src/telemetry.c)
pitrac_host_test(strobe_plan_test strobe_plan_test.c ../src/strobe_plan.c)
pitrac_host_test(strobe_live_test strobe_live_test.c ../src/strobe_live.c ../src/strobe_plan.c)
pitrac_host_test(service_test service_test.c ../src/service.c)
pitrac_host_test(adc_ring_test adc_ring_test.c)
pitrac_host_test(link_test link_test.c ../src/link_frame.c ../src/sha256.c)
pitrac_host_test(fw_update_test fw_update_test.c ../src/fw_update.c ../src/sha256.c
                 ../src/link_proto.c ../src/link_frame.c)
pitrac_host_test(cam_plan_test cam_plan_test.c ../src/cam_plan.c)
pitrac_host_test(cal_math_test cal_math_test.c ../src/cal_math.c)

# Phase 7: the camera handshake's PIO programs, run in a cycle-counting model
# (pio_sim.c). The words come from pioasm -- the SAME program text the firmware
# build assembles -- so the test checks what ships. pioasm is the Pico SDK's,
# installed by the VS Code extension (HANDOFF.md section 4).
find_program(PITRAC_PIOASM pioasm
    HINTS "$ENV{USERPROFILE}/.pico-sdk/tools/2.3.0/pioasm" "$ENV{HOME}/.pico-sdk/tools/2.3.0/pioasm")
if(PITRAC_PIOASM)
    set(pio_gen ${CMAKE_CURRENT_BINARY_DIR}/pio_gen)
    file(MAKE_DIRECTORY ${pio_gen})
    set(pio_headers)
    foreach(prog IN ITEMS cam_handshake strobe_gate strobe_burst)
        add_custom_command(
            OUTPUT ${pio_gen}/${prog}.pio.h
            COMMAND ${PITRAC_PIOASM} -o c-sdk ${CMAKE_CURRENT_LIST_DIR}/../src/${prog}.pio
                    ${pio_gen}/${prog}.pio.h
            DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../src/${prog}.pio
            COMMENT "pioasm ${prog}.pio (host test)")
        list(APPEND pio_headers ${pio_gen}/${prog}.pio.h)
    endforeach()
    pitrac_host_test(cam_pio_test cam_pio_test.c pio_sim.c ../src/cam_plan.c
                     ../src/strobe_plan.c ${pio_headers})
    target_include_directories(cam_pio_test PRIVATE ${pio_gen})
    foreach(test IN ITEMS
        encodings
        loopback
        delayed_early
        delayed_late
        boundary_sweep
        staggered
        never_both
        timeout_one_strobe
        exact_small_counts
        first_delay_zero
        back_to_back
        gate_then_burst
        gate_holds_on_timeout
        nonoverlap_handover
        coherent_property
    )
        add_test(NAME campio.${test} COMMAND cam_pio_test ${test})
    endforeach()

    # Both tests below run firmware functions copied verbatim out of src/, which
    # needs Python.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # The REAL power_fsm.c, service.c, telemetry.c and strobe.c together (image 1.9,
        # review finding V18-F1): the strobe's Pi observation against the FSM's actual
        # timing -- a Pi's 3V3 lost under RUNNING, a late Pi in BENCH_RUNNING,
        # PI_BOOTING, a halt, an orderly shutdown. The harness is adapted from the 1.8
        # independent review's probe (captures/review_2026-11-12/independent_v18_probes/).
        # Image 1.10 (review finding V19-F1): `reset`, `bootsel` and `cfg save`'s
        # guard, copied verbatim out of cli.c and config_store.c, against the same FSM.
        add_custom_command(
            OUTPUT ${pio_gen}/fsm_guards_extract.c
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/extract_functions.py
                    ${pio_gen}/fsm_guards_extract.c
                    ${CMAKE_CURRENT_LIST_DIR}/../src/cli.c reset_guard_ok,reset_still_ok,cmd_reset,cmd_bootsel
                    ${CMAKE_CURRENT_LIST_DIR}/../src/config_store.c
                    cfg_save_blocked_reason,cfg_save_unblock_hint
            DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../src/cli.c
                    ${CMAKE_CURRENT_LIST_DIR}/../src/config_store.c
                    ${CMAKE_CURRENT_LIST_DIR}/extract_functions.py
            COMMENT "copy reset, bootsel and the cfg save guard out of src/ (host test)")
        set_source_files_properties(${pio_gen}/fsm_guards_extract.c PROPERTIES HEADER_FILE_ONLY TRUE)
        pitrac_host_test(strobe_fsm_test strobe_fsm_test.c ../src/strobe.c ../src/power_fsm.c
                         ../src/service.c ../src/telemetry.c ../src/camera.c ../src/shot.c
                         ../src/strobe_plan.c ../src/strobe_live.c ../src/cam_plan.c
                         ${pio_headers} ${pio_gen}/fsm_guards_extract.c)
        target_include_directories(strobe_fsm_test BEFORE PRIVATE sdk_standin ${pio_gen})
        target_compile_definitions(strobe_fsm_test PRIVATE PICO_NO_HARDWARE=1)
        foreach(test IN ITEMS
            sense_lost_ends_session
            late_sense_refuses_arm
            late_sense_ends_session
            halt_keeps_session
            settled_sessions
            pi_booting_refuses_arm
            shutdown_ends_live
            operator_watchdog
            reset_late_pi
            reset_powering_on
            reset_settled
            reset_rise_in_wait
            reset_rise_in_message
        )
            add_test(NAME fsm.${test} COMMAND strobe_fsm_test ${test})
        endforeach()

        # The UNCHANGED strobe.c, camera.c and shot.c -- and the `gpio` command copied
        # verbatim out of cli.c -- against a register model (sdk_standin/), for the
        # independent review's H7-F2 and H7-F3 (2026-11-10).
        add_custom_command(
            OUTPUT ${pio_gen}/cli_gpio_extract.c
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/extract_cli_gpio.py
                    ${CMAKE_CURRENT_LIST_DIR}/../src/cli.c ${pio_gen}/cli_gpio_extract.c
            DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../src/cli.c ${CMAKE_CURRENT_LIST_DIR}/extract_cli_gpio.py
            COMMENT "copy the gpio command out of cli.c (host test)")
        set_source_files_properties(${pio_gen}/cli_gpio_extract.c PROPERTIES HEADER_FILE_ONLY TRUE)
        pitrac_host_test(engine_test engine_test.c ../src/strobe.c ../src/camera.c ../src/shot.c
                         ../src/strobe_plan.c ../src/strobe_live.c ../src/cam_plan.c
                         ${pio_headers} ${pio_gen}/cli_gpio_extract.c)
        target_include_directories(engine_test BEFORE PRIVATE sdk_standin ${pio_gen})
        target_compile_definitions(engine_test PRIVATE PICO_NO_HARDWARE=1)
        foreach(test IN ITEMS
            gate_abort_before_fire
            gate_abort_first_pulse
            gate_abort_between_pulses
            gate_after_completion
            gate_cancel_after_poll
            gate_cancel_active
            gate_irq0_counts_as_opened
            gate_unknown_when_never_armed
            shot_timeout_certified
            shot_timeout_contradicted
            shot_power_abort_mid_burst
            shot_power_abort_before_fire
            shot_fired
            manual_trigger_rail_down
            manual_trigger_fault
            manual_trigger_busy
            manual_trigger_rail_up
            live_no_pi_arms_watchdog
            live_with_pi_no_watchdog
            live_pi_goes_away
            live_pi_appears
            live_pi_sense_lost
            live_pi_sense_rises
            live_arm_refused_unsettled
            live_with_pi_keeps_operator_watchdog
        )
            add_test(NAME engine.${test} COMMAND engine_test ${test})
        endforeach()
    else()
        message(WARNING "Python not found: strobe_fsm_test (the real FSM under the strobe, "
                        "reset, bootsel and cfg save; V18-F1/V19-F1) and engine_test "
                        "(strobe.c/camera.c/shot.c and the gpio command, H7-F2/H7-F3) "
                        "are NOT built.")
    endif()
else()
    message(WARNING "pioasm not found: the camera handshake's PIO tests (cam_pio_test) "
                    "are NOT built. Install the Pico SDK tools, or set PITRAC_PIOASM.")
endif()

foreach(test IN ITEMS
    request_limits
    counts_round_trip
    conversions
    decode_words
)
    add_test(NAME camplan.${test} COMMAND cam_plan_test ${test})
endforeach()

foreach(test IN ITEMS
    geometry
    judge_board4_runs
    judge_floor_boundary
    judge_quality_terms
)
    add_test(NAME calm.${test} COMMAND cal_math_test ${test})
endforeach()

foreach(test IN ITEMS
    v5in_fresh
    v5in_rejects_hw_flags
    v5in_rejects_conv_error
    v5in_skips_newest_group
    v5in_phase_rotation
    v5in_bad_args
    adc_rate_div
    adc_rate_range
)
    add_test(NAME adc.${test} COMMAND adc_ring_test ${test})
endforeach()

foreach(test IN ITEMS
    redundant_on_shutdown
    redundant_on_forceoff
    on_during_startup
    on_during_shutdown
    on_during_fault
    standby_off
    pending_on_shutdown
    pending_on_forceoff
    fresh_on_after_cancel
    shutdown_during_startup
    shutdown_during_pi_boot
    supply_loss
    rail_collapse
    usb_guard
    button_cycle
    long_press
    stale_fault_ack
    request_on_result
    pi_boot_timeout
    pi_shutdown_timeout
    strobe_teardown
    camera_teardown
    supply_unreadable_latch
    supply_not_sampled_latch
    supply_unreadable_rail
    supply_stall_running
    supply_not_sampled_running
    supply_invalid_latch
    supply_invalid_running
    line_follows_pi
    line_bench_released
    tele_clean_shutdown
    tele_shutdown_timeout
    tele_power_cuts
    tele_boot_timeout_ack
    tele_pi_down_unrequested
    tele_glitch_not_counted
    tele_reset_cuts_pi
    reset_during_force_off
    reset_during_clean_off
    shutdown_unconfirmed_times_out
    uart_follows_pi
    uart_released_before_latch
    uart_bench_released
    maybe_present_power_up
    maybe_present_late_pi
)
    add_test(NAME power.${test} COMMAND power_fsm_test ${test})
endforeach()

foreach(test IN ITEMS
    crc_check_value
    encode_vectors
    encode_limits
    decode_round_trip
    every_bit_flip_rejected
    resync_after_junk
    corrupt_then_next
    gap_drops_partial
    reset_keeps_counters
    rx_settle
    sha_vectors
    sha_million_a_chunked
    sha_padding_boundaries
)
    add_test(NAME link.${test} COMMAND link_test ${test})
endforeach()

foreach(test IN ITEMS
    parse_image
    begin_checks
    data_sequence
    end_checks
    apply_needs_running
    install_waits_for_quiet
    install_order
    no_bootable_image_until_last_step
    install_verify_fails
    install_verify_fails_sector0
    install_interrupted
    arm_timeout
    arm_timeout_in_standby_repowers
    reboot_refused
    abort_anytime
    begin_refused_while_installing
    final_header_tbyb_fault
    final_header_faults
    final_header_partial_program
    final_program_interrupted
    cleanup_by_erase
    cleanup_fails
    two_faults_reported_honestly
    first_erase_fails
    reboot_refused_cleanup_fails
    rom_oracle_semantics
    boundary_block_cleanup_erases
    boundary_block_cleanup_fails
    linked_block_cleared
    install_then_buy
    trial_unhealthy_rolls_back
    trial_layout_or_buy_fails
    hang_rolls_back
    wrong_version_is_rollback
    usb_trial_no_note
    repower_only_from_standby
    noise_is_no_note
    torn_note_rejected
    repower_after_reset_in_delay
    repower_after_reset_in_delay_installed
    reset_before_settling_a_buy
    repower_around_acceptance
    repower_refused_is_retried
    repower_fault_ends_it
    abort_cancels_repower
    reset_during_shutdown_repowers
    reset_during_install_repowers
    note_commit_resets
    proto_shapes
    proto_info_fields
    proto_update_flow
)
    add_test(NAME fwu.${test} COMMAND fw_update_test ${test})
endforeach()

foreach(test IN ITEMS
    first_boot
    from_flash
    old_record_ignored
    warm_reset_keeps_unsaved
    power_on_uses_flash
    corrupt_mirror_rejected
    boot_reasons
    power_cut_at_reset
    saturates
    dirty_and_saved
    fault_recorded
    clear_keeps_boot
    names
    torn_release_counts_once
    torn_count_keeps_history
    release_pi
)
    add_test(NAME telemetry.${test} COMMAND telemetry_test ${test})
endforeach()

foreach(test IN ITEMS
    blank_flash_defaults
    save_and_reload
    slots_alternate
    default_then_save_sticks
    old_record_upgrade
    tele_waits_for_quiet_standby
    tele_saves_flash_record_not_ram
    tele_only_record
    verify_catches_lost_write
    tele_retry_and_give_up
    guard_blocks_writes
    overlap_refuses
    threshold_restored_at_boot
    threshold_default_zero
    threshold_out_of_range_ignored
    unblock_hint_names_the_command
    sensed_pi_blocks_writes
)
    add_test(NAME cfg.${test} COMMAND config_store_test ${test})
endforeach()

foreach(test IN ITEMS
    schedule_table
    shed_keeps_spacing
    schedule_rejects_bad_speed
    validate_limits
    clamptest_bounds
    encode_words
    burst_duration
    policy_basic
    policy_exhaustive
    policy_pi_present
    pi_observe
    refusal_texts
    gate_settling
    live_gate_limit
    live_typed_steps
    live_policy_reasons
    live_policy_exhaustive
    live_pi_rule
    live_validate_span
    live_session_cap
)
    add_test(NAME strobe.${test} COMMAND strobe_plan_test ${test})
endforeach()

foreach(test IN ITEMS
    code_amps
    single_pulse
    plateau_excludes_edges
    nothing_detected
    lone_spikes_ignored
    small_current_detected
    burst_segmentation
    dips_merge
    extra_conduction
    short_run_kept
    stuck_baseline
    stuck_early_run
    stuck_on_at_end
    clamp_width_limit
    overcurrent_plateau
    overcurrent_peak
    stuck_outranks_overcurrent
    noisy
    no_data
    verdict_texts
    baseline_split
    ring_copy_avail
    baseline_by_index
    baseline_checked
    coverage
    snapshot_latency_cancel
    acquisition_stopped
    short_gap_pulse
    short_gap_stuck
    short_baseline
    replay_2026_10_12
    replay_2026_10_13_g21
    replay_2026_10_13_g24
    stuck_under_spike
    held_overcurrent
    shape_bounds
    clean_pulses_2026_10_15
    lone_sample_reported
    floor_is_absolute
    residual_between_pulses
    long_run_beyond_stored
    shape_first_in_burst
    shape_bound_at_design_current
    policy_two_of_three
    policy_low_current_dip
    budget_window
    budget_wrap
    budget_full_table
    budget_rejects_bad
    budget_bookings
    interval
    idle_timeout
    cal_next_steps
    cal_steps_cannot_overshoot
    cal_solve_interp
    cal_converges
    cal_ceiling
    session_cap_math
    session_cap_bounds_current
)
    add_test(NAME live.${test} COMMAND strobe_live_test ${test})
endforeach()

foreach(test IN ITEMS
    final_slice_key
    final_slice_fault
    fault_at_entry_is_not_abort
    same_code_relatched
    cleared_fault_is_not_abort
    no_overshoot
    watchdog_kicks
    strobe_hook_every_pass
    disarm_clears_mark
)
    add_test(NAME service.${test} COMMAND service_test ${test})
endforeach()
