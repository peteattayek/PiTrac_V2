cmake_minimum_required(VERSION 3.13)
project(pitrac_host_tests C)

# Native host tests. NOT an RP2350 target: these compile real firmware sources
# (power_fsm.c, strobe_plan.c, strobe_live.c, service.c) against small mocks and
# never talk to the board.
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

pitrac_host_test(power_fsm_test power_fsm_test.c ../src/power_fsm.c)
pitrac_host_test(strobe_plan_test strobe_plan_test.c ../src/strobe_plan.c)
pitrac_host_test(strobe_live_test strobe_live_test.c ../src/strobe_live.c ../src/strobe_plan.c)
pitrac_host_test(service_test service_test.c ../src/service.c)

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
)
    add_test(NAME power.${test} COMMAND power_fsm_test ${test})
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
    refusal_texts
    gate_settling
    live_gate_limit
    live_typed_steps
    live_policy_reasons
    live_policy_exhaustive
    live_validate_span
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
)
    add_test(NAME service.${test} COMMAND service_test ${test})
endforeach()
