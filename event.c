#include "event.h"

#include <stdio.h>

// The maximum number of elements in the event queues. This should allow for
// quite a bit of variablility in timing of the serialization/logging without
// overflow.
#define FIFO_LENGTH 512

void init_event_bus(event_bus_t* eb) {
	queue_init(&eb->hs_fifo, sizeof(event_t), FIFO_LENGTH);
	queue_init(&eb->ls_fifo, sizeof(event_t), FIFO_LENGTH);
}

bool read_event_bus(event_bus_t* eb, event_t* event) {
	// First try the high-speed queue, it will have items more frequently.
	int count = queue_get_level(&eb->hs_fifo);
	if (count != 0) {
		queue_remove_blocking(&eb->hs_fifo, event);
		return true;
	}

	// Next look for something in the low-speed queue.
	count = queue_get_level(&eb->ls_fifo);
	if (count != 0) {
		queue_remove_blocking(&eb->ls_fifo, event);
		return true;
	}

	// If there was nothing to read, indicate so.
	return false;
}

bool write_event_bus(event_bus_t* eb, event_t* event) {
	// Route external adc data events to the high speed queue. Since they
	// are acquired in a more frequent interrupt, we don't want to risk
	// blocking for the low-frequency interrupt to complete, dropping a few
	// samples. Depending on the exact implementation details of the queues
	// and the spinlocks they use, it might be OK to merge these queues,
	// there just wasn't a lot of docs on how to prevent priority inversion
	// with these queues.
	if (event->type == EVENT_EXT_ADC) {
		return queue_try_add(&eb->hs_fifo, event);
	} else {
		return queue_try_add(&eb->ls_fifo, event);
	}
}

bool serialize_event(event_t* event, char* buf, size_t buf_size) {
	// Switch on the event type, each one has different fields and must be
	// handled differently.
	int ret = 0;
	switch (event->type) {
		case EVENT_EXT_ADC:
			ret = snprintf(buf, buf_size, "0,%lld,%d,%d",
					event->timestamp_us,
					event->ext_adc.channel,
					event->ext_adc.data);
			return !(ret < 0) && !(ret >= buf_size);
		case EVENT_IMU:
			ret = snprintf(buf, buf_size, "1,%lld,%f,%f,%f,%f,%f,%f",
					event->timestamp_us,
					event->imu.accel.x,
					event->imu.accel.y,
					event->imu.accel.z,
					event->imu.gyro.x,
					event->imu.gyro.y,
					event->imu.gyro.z);
			return !(ret < 0) && !(ret >= buf_size);
		case EVENT_RES:
			ret = snprintf(buf, buf_size, "2,%lld,%f,%f,%f",
					event->timestamp_us,
					event->res.active_therm_volts,
					event->res.passive_therm_volts,
					event->res.fsr_volts);
			return !(ret < 0) && !(ret >= buf_size);
		case EVENT_DBG:
			ret = snprintf(buf, buf_size, "3,%lld,%s",
					event->timestamp_us,
					event->dbg_msg);
			return !(ret < 0) && !(ret >= buf_size);
		default:
			printf("ERR - unrecognized event type %d", event->type);
			return false;
	}
	return false; // unreachable
}
