/*
 * ft6336u_glue.h - ESP-IDF I2C glue for the platform-agnostic FT6336U driver.
 *
 * Wires struct ft6336_interface to the ESP-IDF new I2C master API
 * (driver/i2c_master.h). Single touch controller per process.
 */

#pragma once

#include "ft6336u.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up an I2C master bus and initialize an FT6336U on it.
 *
 * Creates the bus and device, points @p pinstance's interface at the ESP-IDF
 * I2C transfers, and calls ft6336_init(). Only one controller is supported.
 *
 * @param pinstance Caller-allocated instance to initialize.
 * @param sda_io GPIO number for SDA.
 * @param scl_io GPIO number for SCL.
 * @param speed_hz I2C clock, e.g. 400000.
 * @param threshold Touch-detection threshold (THGROUP); see ft6336_init().
 * @param filter Filter coefficient (FILTER_COE); see ft6336_init().
 *
 * @return FT6336_OK on success, FT6336_ERROR on bus/device/init failure.
 */
int ft6336u_glue_install(struct ft6336_instance *pinstance, int sda_io,
                         int scl_io, uint32_t speed_hz, uint8_t threshold,
                         uint8_t filter);

#ifdef __cplusplus
}
#endif
