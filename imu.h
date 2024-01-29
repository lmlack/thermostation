#ifndef _IMU_H
#define _IMU_H

#include "hardware/i2c.h"

// The IMU is an MPU-6050 6DOF accel/gyro with quite a few features. We use
// relatively few of those features, so our configuration and data reading
// routines are relatively simple.
//
// We may use 2 MPU-6050 in different locations on the device, so each device
// instance must keep track of the I2C hw instance it is connected to.
typedef struct imu_inst {
	// I2C peripheral instance connected to this MPU6050
	i2c_inst_t* i2c;

	// The address of this device on the I2C bus
	uint8_t bus_addr;

	// Scale factors for acceleration and gyro, can be the default or come
	// from a calibration routine.
	float accel_scale;
	float gyro_scale;

	// ID of the IMU. Since we have potentially 2 IMUs connected, this will
	// be either 0 or 1 depending on which IMU it came from. IMU 0 is
	// mounted to the PCB, IMU 1 is attached through the connector.
	int id;
} imu_inst_t;

// Simple 3 element float vector with array or component-level access to
// components.
typedef struct vec3f {
	union {
		float v[3];
		struct {
			float x;
			float y;
			float z;
		};
	};
} vec3f_t;

// Holds the sample data for the IMU, 2 3d vecs for accelerometer and gyro data.
typedef struct imu_sample {
	// ID of the IMU that collected this sample.
	int id;

	// Accel and gyro data
	vec3f_t accel;
	vec3f_t gyro;
} imu_sample_t;

// Initializes IMU given the instance data.
//
// NOTE: Instance struct must be filled out with correct instance-specific
// data, a config struct felt like overkill here so we just fill out the
// structs before calling into the common initialization logic in this
// function.
void init_imu(imu_inst_t* imu, int scl_pin, int sda_pin);

// Reads out all accel/gyro data registers and stores the converted results
// into the sample struct.
//
// Returns 0 on success, non-zero on failure.
int read_imu(imu_inst_t* imu, imu_sample_t* sample);

#endif // _IMU_H
