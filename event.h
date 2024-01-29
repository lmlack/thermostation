#ifndef _EVENT_H
#define _EVENT_H

#include "pico/util/queue.h"

#include "ext_adc.h"
#include "imu.h"
#include "resistive_sensors.h"

// The event system is used to safely process events generated in interrupts on
// one core, and process them in an event loop on another core of the RP2040,
// using queues internally.
typedef struct event_bus {
	// FIFO for high speed ISR events. I didn't have time to carefully
	// analyze the pico queue code to see how it handles priority
	// inversion, but I took a glance and saw the word spinlock, so out of
	// caution until I am sure it is OK to share the same queue between the
	// higher speed and lower speed interrupts, I will maintain two queues
	// and route events to them based on their event type. This is a bit of
	// a leaky abstraction since it requires knowledge that certain events
	// will come from certain ISRs, but that's ok for now.
	queue_t hs_fifo;

	// FIFO for low speed ISR events.
	queue_t ls_fifo;
} event_bus_t;

// The event types are used to determine what data is actually contained in the
// event. When serialized, the type is printed first, followed by the fields of
// whatever event data corresponds to the type. The serialized event strings
// for each type are listed below with the enumeration of the types.
typedef enum event_type {
	// Event with external IMU data.
	//
	// Serialized:
	// "0,<timestamp (uint64_t)>,<channel (int)>,<data (int16_t)>"
	EVENT_EXT_ADC = 0,

	// Event with IMU data
	//
	// Serialized (a for accel data, g for gyro data):
	// "1,<timestamp (uint64_t)>,<a.x (float)>,<a.y (float)>,<a.z (float)>,
	// <g.x (float)>,<g.y (float)>,<g.z (float)>"
	EVENT_IMU = 1,

	// Event with resistive sensor data
	//
	// Serialized:
	// "2,<timestamp (uint64_t)>,<active therm volts (float)>,
	// <passive therm volts (float)>,<fsr volts (float)>"
	EVENT_RES = 2,

	// Event with a debug log to forward to the host
	//
	// Serialized:
	// "3,<timestamp (uint64_t)>,
	// <message (ascii string terminated by newline)>"
	EVENT_DBG = 3,
} event_type_t;

// The events are tagged unions, each event type corresponds to some kind of
// sample from a sensor. The sampling interrupts write events to the event bus,
// and the event loop reads and serializes them (doing costly string formatting
// operations) in the background.
//
// Each event also contains a timestamp, which holds the enumber of
// microseconds since boot when this event was generated.
typedef struct event {
	event_type_t type;
	uint64_t timestamp_us;
	union {
		imu_sample_t imu;
		ext_adc_sample_t ext_adc;
		res_sensor_sample_t res;
		char* dbg_msg;
	};
} event_t;

// Initializes the event bus, must be called before any events are written.
void init_event_bus(event_bus_t* eb);

// Reads a single event from the bus, storing it in the given event pointer.
//
// Returns true if an event was read, and false if there were no events
// available to read.
bool read_event_bus(event_bus_t* eb, event_t* event);

// Writes a single event to the bus.
//
// Returns true if the write succeeded, and false if the write failed - can
// occur if there are too many buffered events due to slow formatting/sending,
// but should never happen normally.
//
// Returns true if the event was written succesfully, or false if it could not
// be written.
bool write_event_bus(event_bus_t* eb, event_t* event);


// Serializes an event into the simple string format, serializing into the
// given buffer. If the serialized event exceeds the max length, it returns
// false and truncates the event string. If the event serialized successfully,
// it returns true.
bool serialize_event(event_t* event, char* buf, size_t buf_size);

#endif // _EVENT_H
