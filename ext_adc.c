#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "ext_adc.h"

// Pins for ADS1018-Q1 SPI interface
#define EXT_ADC_PIN_MISO 19
#define EXT_ADC_PIN_CS 17
#define EXT_ADC_PIN_SCK 18
#define EXT_ADC_PIN_MOSI 16

// Channel mux config bits. We only use the top few values but the valid mux
// bits are:
// 000 = AINP is AIN0 and AINN is AIN1 (default)
// 001 = AINP is AIN0 and AINN is AIN3
// 010 = AINP is AIN1 and AINN is AIN3
// 011 = AINP is AIN2 and AINN is AIN3
// 100 = AINP is AIN0 and AINN is GND
// 101 = AINP is AIN1 and AINN is GND
// 110 = AINP is AIN2 and AINN is GND
// 111 = AINP is AIN3 and AINN is GND
#define EXT_ADC_CH0 4
#define EXT_ADC_CH1 5
#define EXT_ADC_CH2 6
#define EXT_ADC_CH3 7

// Gain config bits, we don't bother defining the redundant ones, valid bits
// are:
// 000 = FSR is ±6.144 V(1)
// 001 = FSR is ±4.096 V(1)
// 010 = FSR is ±2.048 V (default)
// 011 = FSR is ±1.024 V
// 100 = FSR is ±0.512 V
// 101 = FSR is ±0.256 V
// 110 = FSR is ±0.256 V
// 111 = FSR is ±0.256 V
#define EXT_ADC_GAIN_6V 0
#define EXT_ADC_GAIN_4V 1
#define EXT_ADC_GAIN_2V 2
#define EXT_ADC_GAIN_1V 3
#define EXT_ADC_GAIN_512mV 4
#define EXT_ADC_GAIN_256mV 5

// Helper to build a config register value, encapsulating all the hardcoded
// offsets from the ADS1018-Q1 datasheet. The ADS1018-Q1 has just 15 config
// bits, so rather than a typical register map type interface where you'd have
// to write a minimum of one register address byte and one register data byte,
// it just has you write the the whole 16b on every SPI transaction each time.
static inline uint16_t ext_adc_config(bool start, int mux, int gain) {
	uint16_t config = 0;

	// Writing a one here will trigger the next conversion to start in
	// single-shot mode.
	config |= (start ? 1 : 0) << 15;

	// Set the mux to the desired channel.
	config |= (mux & 7) << 12;

	// Set the gain to the desired value.
	config |= (gain & 7) << 9;

	// Set the mode to either continuos (0) or single-shot (1). We always
	// want single-shot so we can control the rate with a timer interrupt.
	config |= 1 << 8;

	// Set the sample rate, which sets how long each conversion will take.
	// The longer the conversion, the less noise and power consumption, so
	// we want this to be the smallest value that's greater than
	// (4 channels)*(500 samples/s), or 2400sps, from the given options:
	// 000 = 128 SPS
	// 001 = 250 SPS
	// 010 = 490 SPS
	// 011 = 920 SPS
	// 100 = 1600 SPS (default)
	// 101 = 2400 SPS
	// 110 = 3300 SPS
	// 111 = Not Used
	config |= 6 << 5;

	// Never read internal temp sensor, temp sensor bit can be
	// 0 = ADC mode (default)
	// 1 = Temperature sensor mode
	config |= 0 << 4;

	// Disable internal pull-up on DOUT/(active low)
	config |= 0 << 3;

	// Always set bits 2:1 to 01, otherwise the write will be ignored.
	config |= 1 << 1;

	return config;
}	

// Helper to convert ADS1018-Q1 config channel setting to an actual channel
// number.
static inline int adc_channel_to_channel_num(int adc_ch) {
	return adc_ch - 4;
}

// Software managed chip select - set CS low to begin a SPI transaction.
static inline void ext_adc_select(void) {
	gpio_put(EXT_ADC_PIN_CS, 0);
}

// Software managed chip select - set CS high to end a SPI transaction.
static inline void ext_adc_deselect(void) {
	gpio_put(EXT_ADC_PIN_CS, 1);
}

void init_ext_adc(ext_adc_t* ext_adc) {
	// Connected to SPI0, 900kHz
	//
	// TODO: Optimize clock rate - we can go faster but the datasheet
	// mentioned maybe adding some delays between successive readings in
	// some cases with SPI clocks >1MHz, so we shoudl check on that before
	// increasing.
	spi_init(spi0, 900*1000);

	// The ADS1018-Q1 supports 16b and 32b transactions, and wants SPI mode
	// 1 where the clock  idles low and data is sampled on the falling
	// clock edges.
	spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);

	// Set up pinmux to connect pins to SPI bus.
	gpio_set_function(EXT_ADC_PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(EXT_ADC_PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(EXT_ADC_PIN_MOSI, GPIO_FUNC_SPI);

	// Set up CS pin as output
	gpio_init(EXT_ADC_PIN_CS);
	gpio_set_dir(EXT_ADC_PIN_CS, GPIO_OUT);

	// Set channel to 0 in our internal state, and go ahead and configure
	// the ADS1018-Q1 to the desired settings and begin the CH0 conversion
	// so that the CH0 sample is ready later and we can start the normal
	// read cycle in an interrupt.
	const uint16_t initial_config = ext_adc_config(true, EXT_ADC_CH0, EXT_ADC_GAIN_4V); 
	ext_adc_select();
	spi_write16_blocking(spi0, &initial_config, 1); 
	ext_adc_deselect();
}

int read_ext_adc(ext_adc_t* ext_adc, ext_adc_sample_t* sample) {
	// Each time we read from the SPI bus, we write in a configuration used
	// for the next conversion, so the first time we read we need to select
	// CH1 in the mux, that way we read CH1 the next read.
	const int next_channel_map[4] = {
		EXT_ADC_CH1,
		EXT_ADC_CH2,
		EXT_ADC_CH3,
		EXT_ADC_CH0,
	};

	const int curr_channel = ext_adc->current_channel;
	const int next_channel = next_channel_map[curr_channel];

	const uint16_t config = ext_adc_config(true, next_channel, EXT_ADC_GAIN_4V); 
	int16_t data = 0;
	ext_adc_select();
	spi_write16_read16_blocking(spi0, &config, &data, 1); 
	ext_adc_deselect();

	// This is only a 12 bit ADC with the data left aligned, so we shift by
	// 4b to right align the data to be in the expected 0-4095 range.
	sample->channel = curr_channel;
	sample->data = data >> 4;

	// Update internal state for next reading.
	ext_adc->current_channel = adc_channel_to_channel_num(next_channel);

	return 0;
}
