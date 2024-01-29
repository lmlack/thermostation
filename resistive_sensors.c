#include "hardware/gpio.h"
#include "hardware/adc.h"

#include "resistive_sensors.h"

// These pins are connected to the SPST solid state witch that controls the
// active thermistor heating. The switch can be enabled or disabled. When
// disabled no connections are made. When enabled, it will always connect the
// active thermistor to either 20V or precision 3.3V depending on the level of
// the SW pin.
#define SW_SEL_PIN 4
#define SW_EN_PIN 5

// These are the pico pin numbers for the 3 ADC pins connected to the sensors.
#define AT_ADC_PIN 26
#define PT_ADC_PIN 27
#define FSR_ADC_PIN 28

// The internal ADC has a mux that connects it to one gpio at a time. Before
// sampling an input, the mux must be set to the right channel. These are the
// mappings between channel number and sensor.
#define AT_ADC_CHANNEL 0
#define PT_ADC_CHANNEL 1
#define FSR_ADC_CHANNEL 2

void init_resistive_sensors(void) {
	// Configure pinmux for ADC inputs
	adc_gpio_init(AT_ADC_PIN);
	adc_gpio_init(PT_ADC_PIN);
	adc_gpio_init(FSR_ADC_PIN);

	// Configure GPIO to control high voltage switch IC.
	gpio_init(SW_SEL_PIN);
	gpio_init(SW_EN_PIN);
	gpio_set_dir(SW_SEL_PIN, GPIO_OUT);
	gpio_set_dir(SW_EN_PIN, GPIO_OUT);

	// By default, set the switch to connect it to 3.3V, and enable the
	// switch.
	gpio_put(SW_SEL_PIN, 1);
	gpio_put(SW_EN_PIN, 1);

	// Finally enable the internal ADC and disable the free-running mode.
	// We need to disable free running mode since we don't control when
	// exactly it collects the samples when free-running, it just returns
	// the last collected sample, so for the active thermistor if we switch
	// to 3.3V and then call adc_read(), the sample returned may have been
	// collected a while ago when it was still connected to 20V. So we
	// simply disable free-running mode, and acquire each sample in
	// single-shot mode, waiting for it to complete.
	adc_init();
	adc_run(false);
}

int read_resistive_sensors(res_sensor_sample_t* data) {
	// 12-bit conversion, assume max value == ADC_VREF == 3.3 V
	const float volts_conversion_factor = 3.3f / (1 << 12);

	// First we switch the active thermistor into measure mode. It takes a
	// few hundred ns to settle out, so by switching it here and reading it
	// last, the delay caused by reading the other two channels allows the
	// switch to settle.
	set_active_therm_heat(false);

	// Read all the channels and store the converted results in the sample.
	//
	// TODO: We could do some filtering here if noise is a problem.
	//
	// TODO: Per-channel calibration could be needed if ADC INL is bad.
	adc_select_input(FSR_ADC_CHANNEL);
	data->fsr_volts = adc_read() * volts_conversion_factor;

	adc_select_input(PT_ADC_CHANNEL);
	data->passive_therm_volts = adc_read() * volts_conversion_factor;

	adc_select_input(AT_ADC_CHANNEL);
	data->active_therm_volts = adc_read() * volts_conversion_factor;

	return 0;
}

void set_active_therm_heat(bool heat) {
	if (heat) {
		gpio_put(SW_SEL_PIN, 0);
	} else {
		gpio_put(SW_SEL_PIN, 1);
	}
}
