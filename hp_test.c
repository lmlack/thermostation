#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "pico/util/queue.h"

#define SW_SEL_PIN 4
#define SW_EN_PIN 5
#define AT_ADC_PIN 26
#define AT_ADC_CHANNEL 0

#define PT_ADC_CHANNEL 1
#define FSR_ADC_CHANNEL 2

#define EXT_ADC_PIN_MISO 19
#define EXT_ADC_PIN_CS 17
#define EXT_ADC_PIN_SCK 18
#define EXT_ADC_PIN_MOSI 16

#define EXT_ADC_CH0 4
#define EXT_ADC_CH1 5
#define EXT_ADC_CH2 6
#define EXT_ADC_CH3 7

#define EXT_ADC_GAIN_6V 0
#define EXT_ADC_GAIN_4V 1
#define EXT_ADC_GAIN_2V 2
#define EXT_ADC_GAIN_1V 3
#define EXT_ADC_GAIN_512mV 4
#define EXT_ADC_GAIN_256mV 5

#define TIMER_RATE_HZ 500
#define FIFO_LENGTH 32

typedef struct int_adc_data {
	float fsr;
	float active_thermistor;
	float passive_thermistor;
} int_adc_data_t;

typedef struct ext_adc_data {
	int channel;
	int16_t sample;
} ext_adc_data_t;

// Global FIFOs to coordinate communication between timer interrupts and main
// loop.
queue_t ext_adc_data_fifo;
queue_t int_adc_data_fifo;

void init_therm_hw() {
	adc_gpio_init(AT_ADC_PIN);
	
	gpio_init(SW_SEL_PIN);
	gpio_init(SW_EN_PIN);
	
	gpio_set_dir(SW_SEL_PIN, GPIO_OUT);
	gpio_set_dir(SW_EN_PIN, GPIO_OUT);
	
	//Sets pin to 3.3V on startup
	gpio_put(SW_SEL_PIN, 1);
	gpio_put(SW_EN_PIN, 1);
}

uint16_t ext_adc_config(bool start, int mux, int gain) {
	uint16_t config = 0;
	config |= (start ? 1 : 0) << 15;
	config |= (mux & 7) << 12;
	config |= (gain & 7) << 9;
	config |= 1 << 8;
	config |= 6 << 5;
	config |= 0 << 4;
	config |= 0 << 3;
	config |= 1 << 1;

	return config;
}	


void ext_adc_select(){
	gpio_put(EXT_ADC_PIN_CS, 0);
}

void ext_adc_deselect(){
	gpio_put(EXT_ADC_PIN_CS, 1);
}

void init_ext_adc() {
	//connected to SPI0, 500kHz
	spi_init(spi0, 900*1000);
	spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);
	gpio_set_function(EXT_ADC_PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(EXT_ADC_PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(EXT_ADC_PIN_MOSI, GPIO_FUNC_SPI);

	//Software chip select
	gpio_init(EXT_ADC_PIN_CS);
	gpio_set_dir(EXT_ADC_PIN_CS, GPIO_OUT);
	gpio_put(EXT_ADC_PIN_CS, 1);

	const uint16_t initial_config = ext_adc_config(true, EXT_ADC_CH0, EXT_ADC_GAIN_4V); 
	ext_adc_select();
	spi_write16_blocking(spi0, &initial_config, 1); 
	ext_adc_deselect();
}


float read_active_thermistor() {
	// 12-bit conversion, assume max value == ADC_VREF == 3.3 V
	const float conversion_factor = 3.3f / (1 << 12);
	adc_select_input(AT_ADC_CHANNEL);

	uint16_t result = adc_read();
	result = adc_read();
	result = adc_read();

	//printf("Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
	return result*conversion_factor;
}

float read_passive_thermistor() {
	// 12-bit conversion, assume max value == ADC_VREF == 3.3 V
	const float conversion_factor = 3.3f / (1 << 12);
	adc_select_input(PT_ADC_CHANNEL);

	uint16_t result = adc_read();

	//printf("Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
	return result*conversion_factor;
}

float read_fsr() {
	// 12-bit conversion, assume max value == ADC_VREF == 3.3 V
	const float conversion_factor = 3.3f / (1 << 12);
	adc_select_input(FSR_ADC_CHANNEL);
	uint16_t result = adc_read();
	//printf("Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
	return result*conversion_factor;
}

static inline int adc_channel_to_channel_num(int adc_ch) {
	return adc_ch - 4;
}

bool ext_adc_timer_callback(repeating_timer_t *rt){
	static int channel = 0;

	const int next_channel[4] = {
		EXT_ADC_CH1,
		EXT_ADC_CH2,
		EXT_ADC_CH3,
		EXT_ADC_CH0,
	};

	// Collect all 4 external adc channels. Each time we read from the SPI
	// bus, we write in a configuration used for the next conversion, so
	// the first time we read we need to select CH1 in the mux, that way we
	// read CH1 the next read. We select CH0 in the mux when writing in the
	// last config, so that the next time the interrupt is called, CH0 is
	// read. This means CH0 is delayed by 1 sample, or 2ms, but that should
	// be ok.
	//
	// This is only a 12 bit ADC with the data left aligned, so we shift by
	// 4b to right align the data to be in the expected 0-4095 range.
	const uint16_t config = ext_adc_config(true, next_channel[channel], EXT_ADC_GAIN_4V); 
	int16_t data = 0;
	ext_adc_select();
	spi_write16_read16_blocking(spi0, &config, &data, 1); 
	ext_adc_deselect();

	ext_adc_data_t d;
	d.channel = channel;
	d.sample = data >> 4;

	channel = adc_channel_to_channel_num(next_channel[channel]);

	if (!queue_try_add(&ext_adc_data_fifo, &d)) {
	    printf("FIFO was full\n");
	}

	return true;
}

bool int_adc_timer_callback(repeating_timer_t *rt){
	int_adc_data_t d;

	// Next we collect 3 readings using the RP2040's internal ADC.
	d.passive_thermistor = read_passive_thermistor();
	d.fsr = read_fsr();
	// XXX

	// In order to read the active thermistor, we must ensure the switch is
	// on 3.3V measurement voltage. Then we can take a reading, and if it's
	// too cold, we switch on the heating for 2ms, until the next time we
	// take a reading.
	gpio_put(SW_SEL_PIN, 1);
	const float at_voltage = read_active_thermistor();
	d.active_thermistor = at_voltage;
	if (at_voltage < 2.1) {
		gpio_put(SW_SEL_PIN, 0);
	}

	if (!queue_try_add(&int_adc_data_fifo, &d)) {
	    printf("FIFO was full\n");
	}

	return true;
}


int main() {
	stdio_init_all();
	adc_init();
	adc_run(false);

	init_ext_adc();

	init_therm_hw();
	
	// Before setting up the timer interrupt, we must initialize the
	// queues. Since the timer interrupts will write to the queues, we can't
	// start the timers until they're ready.
	queue_init(&int_adc_data_fifo, sizeof(int_adc_data_t), FIFO_LENGTH);
	queue_init(&ext_adc_data_fifo, sizeof(ext_adc_data_t), FIFO_LENGTH);

	// Set up the timer to fire at 500Hz. Negative timeout means that the
	// delay should be the delay between callbacks starting, if it was
	// posititve then it would delay between the end of one callback and
	// the start of the next. This is insane, I do not know why delay
	// between callback starts is not the default.
	repeating_timer_t timer1;
	repeating_timer_t timer2;
	if(!add_repeating_timer_us(-1000000/500, int_adc_timer_callback, NULL, &timer1)){
		printf("failed to add timer\n");
		return 1;
	}
	if(!add_repeating_timer_us(-1000000/2000, ext_adc_timer_callback, NULL, &timer2)){
		printf("failed to add timer\n");
		return 1;
	}

	while (true) {
		// Each time through the loop, read one sample if any are available.
		int count = queue_get_level(&int_adc_data_fifo);
		if (count != 0) {
			int_adc_data_t d;
			queue_remove_blocking(&int_adc_data_fifo, &d);

			// Log the data out of the USB serial interface.	
			printf("int,%f,%f,%f\r\n",
				d.fsr,
				d.active_thermistor,
				d.passive_thermistor
			);

			// Write sample to SD card
			// XXX
		}
		count = queue_get_level(&ext_adc_data_fifo);
		if (count != 0) {
			ext_adc_data_t d;
			queue_remove_blocking(&ext_adc_data_fifo, &d);

			// Log the data out of the USB serial interface.	
			printf("ext,%d,%d\r\n",
				d.channel,
				d.sample
			);

			// Write sample to SD card
			// XXX
		}


		// sleep_ms(2);
	}
}

