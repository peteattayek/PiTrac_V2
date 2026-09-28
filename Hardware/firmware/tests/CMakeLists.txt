cmake_minimum_required(VERSION 3.13)
project(pitrac_power_tests C)

include(CTest)

add_executable(power_fsm_test
    power_fsm_test.c
    ../src/power_fsm.c
)
target_include_directories(power_fsm_test PRIVATE mocks ../src)
set_target_properties(power_fsm_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(power_fsm_test PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(power_fsm_test PRIVATE -Wall -Wextra -Werror)
endif()

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
)
    add_test(NAME power.${test} COMMAND power_fsm_test ${test})
endforeach()
