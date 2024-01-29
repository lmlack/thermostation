#ifndef _EXT_ADC_H
#define _EXT_ADC_H

// The external ADC is an ADS1018-Q1 connected over SPI. It is a very simple
// device that reads only one channel at a time through an analog mux,
// requiring us to manually switch the mux each time a sample is read. That
// means if we want to capture N samples per second, we actually have to sample
// the ADC at 4*N samples per second, sampling each of the 4 channels
// round-robbin within the desired sampling period. That means that the samples
// for each channel are not synced, and are slightly out of phase with
// eachother, but that doesn't matter too much for our application. We just
// have to keep track of a little state on the mcu and sample faster.
typedef struct ext_adc {
	// Keeps track of the "current channel", which was written into the ADC
	// config register, so that when the next sample is read out, it will
	// correspond with the "current channel".
	int current_channel;
} ext_adc_t;

// Holds the sample data for the external ADC. Each sample is associated with a
// channel, and only one at a time can be read.
typedef struct ext_adc_sample {
	int channel;
	int16_t data;
} ext_adc_sample_t;

// Initializes SPI interface to communicate with the ADS1018-Q1, and
// initializes the instance data.
void init_ext_adc(ext_adc_t* ext_adc);

// Reads one sample from the ADS1018-Q1, which will be written into the given
// sample struct. It is expected that this will be periodically called in an
// interrupt to sample the ADC at a known, constant rate. This will update the
// ADS1018-Q1's mux and begin collecting the next sample.
//
// Returns 0 on success, non-zero on failure.
int read_ext_adc(ext_adc_t* ext_adc, ext_adc_sample_t* sample);

#endif // _EXT_ADC_H
