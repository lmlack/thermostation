#ifndef _RESISTIVE_SENSORS_H
#define _RESISTIVE_SENSORS_H

// Resistive sensors are the active and passive thermistor (AT and
// PT), and the force sensing resistor (FSR). These are all connected
// electrically similarly, each on one of the 3 available RP2040
// internal ADC pins, so we group the functionality to read them here.
//
// The active thermistor additionally has a few GPIOs to control its
// switching behavior to heat it or measure it, that functionality is
// also provided here.


// Holds the sample data for the resistive sensors.
//
// TODO: convert from volts to degrees and maybe force internally using
// per-board calibration data.
typedef struct res_sensor_sample {
	float active_therm_volts;
	float passive_therm_volts;
	float fsr_volts;
} res_sensor_sample_t;

// Initialize pins, internal ADC, and other hardware to read the resistive
// sensors.
void init_resistive_sensors(void);

// Reads the resistive sensors and writes the data to the given sample struct.
//
// Returns 0 on success, non-zero on failure.
//
// NOTE: This will automatically stop heating on the active thermistor, since
// it must be switched to measure mode during the measurement. It is left this
// way for convenience - so that the main loop can re-enable it only if it is
// needed.
int read_resistive_sensors(res_sensor_sample_t* data);

// Toggles heating on the active thermistor - if heat is true, it will be
// connected to 20V heating, otherwise it will be connected to the 3.3V
// precision measurement source.
void set_active_therm_heat(bool heat);

#endif // _RESISTIVE_SENSORS_H
