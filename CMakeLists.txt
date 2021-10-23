cmake_minimum_required(VERSION 3.12)

#set(PICO_PLATFORM host)
include(pico_sdk_import.cmake)

project(pwm_test)

pico_sdk_init()

set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS
    $<$<CONFIG:Debug>:PARAM_ASSERTIONS_ENABLE_ALL=1>
)

add_executable(pwm_test
    main.c
)

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(pwm_test 
    pico_bootsel_via_double_reset    
    pico_stdlib 
    hardware_dma
    hardware_irq
    hardware_pwm
)

# enable usb output, disable uart output
#pico_enable_stdio_usb(pwm_test 1)
#pico_enable_stdio_uart(pwm_test 0)

#pico_set_binary_type(pwm_test no_flash)

# create map/bin/hex file etc.
pico_add_extra_outputs(pwm_test)
