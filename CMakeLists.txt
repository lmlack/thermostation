cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(haptic_project)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of your project
add_executable(hp_test
	ext_adc.c
	imu.c
	event.c
	resistive_sensors.c
    hp_test.c
)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(hp_test
	pico_multicore
	pico_stdlib
	hardware_adc
	hardware_spi
	hardware_i2c
)

pico_enable_stdio_usb(hp_test 1)
pico_enable_stdio_uart(hp_test 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(hp_test)
