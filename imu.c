#include "hardware/gpio.h"

#include "imu.h"

// Registers for MPU-6050
#define MPU6050_WHO_AM_I	0x75
#define MPU6050_GYRO_CONFIG	0x1B
#define MPU6050_ACCEL_CONFIG	0x1C
#define MPU6050_ACCEL_XOUT_H	0x3B
#define MPU6050_GYRO_XOUT_H	0x43
#define MPU6050_PWR_MGMT_1	0x6B

// Helper function to do a single simple register write.
static inline void imu_reg_write(imu_inst_t* imu, const uint8_t reg, const uint8_t val) {
	uint8_t bytes[2] = {reg, val};
	i2c_write_blocking(imu->i2c, imu->bus_addr, bytes, 2, false);
}

void init_imu(imu_inst_t* imu, int scl_pin, int sda_pin) {
	// Initialize I2C port at 400kHz
	i2c_init(imu->i2c, 400*1000);

	// Initialize I2C pins
	gpio_set_function(sda_pin, GPIO_FUNC_I2C);
	gpio_set_function(scl_pin, GPIO_FUNC_I2C);

	// Set IMU registers
	//
	// TODO: consider if we should use a different clock source than the
	// default 8MHz internal oscillator.
	//
	// TODO: read whoami register and enable/disable the IMU.

	// We must write to the pwr mgmt 1 register to wake up the chip, just
	// write all 0's the default value.
	imu_reg_write(imu, MPU6050_PWR_MGMT_1, 0x00);

	// Set to full +/-16g range
	// 
	// 0 ±2g 16384 LSB/g
	// 1 ±4g  8192 LSB/g
	// 2 ±8g  4096 LSB/g
	// 3 ±16g 2048 LSB/g
	const uint8_t accel_fsr = 3;
	const float lsb_per_g[4] = {
		16384.0f,
		8192.0f,
		4096.0f,
		2048.0f,
	};
	const uint8_t accel_config = (accel_fsr << 3);
	imu_reg_write(imu, MPU6050_ACCEL_CONFIG, accel_config);
	imu->accel_scale = 1.0f/lsb_per_g[accel_fsr];

	// Set gyro 2000dps
	//
	// 0 ± 250  °/s 131 LSB/°/s
	// 1 ± 500  °/s 65.5 LSB/°/s
	// 2 ± 1000 °/s 32.8 LSB/°/s
	// 3 ± 2000 °/s 16.4 LSB/°/s
	const uint8_t gyro_fsr = 3;
	const float lsb_per_deg_s[4] = {
		131.0f,
		65.5f,
		32.8f,
		16.4f,
	};
	const uint8_t gyro_config = (gyro_fsr << 3);
	imu_reg_write(imu, MPU6050_GYRO_CONFIG, gyro_config);
	imu->gyro_scale = 1.0f/lsb_per_deg_s[gyro_fsr];
}

int read_imu(imu_inst_t* imu, imu_sample_t* sample) {
	uint8_t reg_addr = 0;
	uint8_t bytes[6] = {0};

	// Read accel data from 6 data registers, high and low bytes separated,
	// in the following order: XH, XL, YH, YL, ZH, ZL
	reg_addr = MPU6050_ACCEL_XOUT_H;
	i2c_write_blocking(imu->i2c, imu->bus_addr, &reg_addr, 1, true);
	i2c_read_blocking(imu->i2c, imu->bus_addr, bytes, 6, false);

	// Reconstruct samples from individual bytes
	sample->accel.x = imu->accel_scale * (int16_t)((bytes[0]<< 8) | bytes[1]);
	sample->accel.y = imu->accel_scale * (int16_t)((bytes[2]<< 8) | bytes[3]);
	sample->accel.z = imu->accel_scale * (int16_t)((bytes[4]<< 8) | bytes[5]);

	// Read gyro data from 6 data registers, high and low bytes separated,
	// in the following order: XH, XL, YH, YL, ZH, ZL
	reg_addr = MPU6050_GYRO_XOUT_H;
	i2c_write_blocking(imu->i2c, imu->bus_addr, &reg_addr, 1, true);
	i2c_read_blocking(imu->i2c, imu->bus_addr, bytes, 6, false);

	// Reconstruct samples from individual bytes
	sample->gyro.x = imu->gyro_scale * (int16_t)((bytes[0]<< 8) | bytes[1]);
	sample->gyro.y = imu->gyro_scale * (int16_t)((bytes[2]<< 8) | bytes[3]);
	sample->gyro.z = imu->gyro_scale * (int16_t)((bytes[4]<< 8) | bytes[5]);

	return 0;
}
