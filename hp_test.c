#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/i2c.h"

#include "event.h"
#include "ext_adc.h"
#include "imu.h"
#include "resistive_sensors.h"


// I2C addresses for MPU-6050s
#define IMU_ADDR	0x68
#define IMU2_ADDR	0x69

// Pins for MPU-6050s
#define IMU_SCL 11
#define IMU_SDA 10
#define IMU_INT 7

#define TIMER_RATE_HZ 500

// Global struct instances are shared between the ISRs and in the case of the
// event bus, even the second core.
event_bus_t event_bus;
ext_adc_t ext_adc;
imu_inst_t imu0;
imu_inst_t imu1;

// This high speed timer callback runs 4x faster than the low speed one, to
// acquire external adc channels at the same rate.
static bool hs_timer_callback(repeating_timer_t *rt){
	// Read ext adc data into event.
	event_t event;
	event.type = EVENT_EXT_ADC;
	if (read_ext_adc(&ext_adc, &event.ext_adc)) {
		printf("ERR - failed to read ext ADC\r\n");
	}

	// Timestamp the event then write it onto the event bus for eventual
	// serialization and transmission/logging.
	event.timestamp_us = to_us_since_boot(get_absolute_time());
	if (!write_event_bus(&event_bus, &event)) {
		printf("ERR - failed to write high speed event\r\n");
	}

	// Returning true from a pico "timer alarm callback" means that we want
	// the callback to keep running - definitely return true here or this
	// will only run once!
	return true;
}

// This low speed timer callback runs at 500Hz and reads most of the sensors,
// as well as handles the active thermistor control loop.
static bool ls_timer_callback(repeating_timer_t *rt){
	// Read resistive sensor data into an event and write it.
	event_t res_event;
	res_event.type = EVENT_RES;
	if (read_resistive_sensors(&res_event.res)) {
		printf("ERR - failed to read resistive sensors\r\n");
	}
	res_event.timestamp_us = to_us_since_boot(get_absolute_time());
	if (!write_event_bus(&event_bus, &res_event)) {
		printf("ERR - failed to write low speed event\r\n");
	}

	// Handle the active thermistor temperature control here. If it's below
	// the threshold, set it to heat.
	//
	// TODO: configure this in degrees C from the SD card settings or over
	// the serial console or something.
	if (res_event.res.active_therm_volts < 1.8f) {
		set_active_therm_heat(true);
	}

	// Read resistive sensor data into an event and write it.
	event_t imu0_event;
	imu0_event.type = EVENT_IMU;
	if (read_imu(&imu0, &imu0_event.imu)) {
		printf("ERR - failed to read imu\r\n");
	}
	imu0_event.timestamp_us = to_us_since_boot(get_absolute_time());
	if (!write_event_bus(&event_bus, &imu0_event)) {
		printf("ERR - failed to write low speed event\r\n");
	}

	// Returning true from a pico "timer alarm callback" means that we want
	// the callback to keep running - definitely return true here or this
	// will only run once!
	return true;
}

// This runs forever processing events from the event bus, serializing them and
// logging them over the uart and onto the SD card.
static void event_loop() {
	event_t event;
	char buf[256];
	while (true) {
		// Spin until an event is available.
		if (!read_event_bus(&event_bus, &event)) {
			continue;
		}

		// Serialize the event into the string, and if the
		// serialization succeeded, log it!
		if (!serialize_event(&event, buf, 256)) {
			printf("ERR - failed to serialize event\r\n");
			continue;
		}

		// Log to uart
		printf("%s\r\n", buf);

		// Log to SD card
		//
		// TODO
	}
}


int main() {
	stdio_init_all();

	init_resistive_sensors();
	init_ext_adc(&ext_adc);

	// Configure IMU 0 device specific settings and initialize it.
	imu0 = (imu_inst_t){
		.i2c = i2c1,
		.bus_addr = IMU_ADDR,
		.id = 0,
	};
	init_imu(&imu0, IMU_SCL, IMU_SDA);

	init_event_bus(&event_bus);
	
	// Set up the timers to fire at 500Hz and 2KHz. Negative timeout means
	// that the delay should be the delay between callbacks starting, if it
	// was posititve then it would delay between the end of one callback
	// and the start of the next. This seems insane, I do not know why
	// delay between callback starts is not the default.
	repeating_timer_t timer1;
	repeating_timer_t timer2;
	if(!add_repeating_timer_us(-1000000/500, ls_timer_callback, NULL, &timer1)){
		printf("failed to add timer\n");
		return 1;
	}
	if(!add_repeating_timer_us(-1000000/2000, hs_timer_callback, NULL, &timer2)){
		printf("failed to add timer\n");
		return 1;
	}

	// Launch event loop on second core
	multicore_launch_core1(event_loop);

	while (true) {
		tight_loop_contents();
	}
}

