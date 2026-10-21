/*
 * dt195_i2c.h - controller register access and I2C bridge for the PIX-DT195.
 *
 * The MB86M01 "Felis" controller exposes a vendor register space over a USB
 * control request (bRequest 0xBC). The on-board demod/tuner chips hang off the
 * controller's I2C bus, reached by writing a small command through a register
 * "data window" and a trigger register, then reading back an ACK.
 *
 * Register framing (bRequest 0xBC, bmRequestType 0x40/0xC0, address big-endian)
 * and the I2C window/trigger register addresses were reconstructed from
 * DT195Lib's ControlRegisterWrite / ControlWriteI2c. The I2C chip addresses
 * come from the chip-manager constructors in TunerManager::Initialize.
 *
 * No conditional-access logic lives here; this is plain bus plumbing.
 */
#ifndef DT195_I2C_H
#define DT195_I2C_H

#include <stdint.h>

#include "dt195.h"

/* Vendor control request used for controller register access. */
#define DT195_VREQ_REG 0xBC

/* Controller registers used to bridge to the I2C bus (from ControlWriteI2c). */
#define DT195_REG_I2C_WINDOW 0x823BEu /* I2C payload data window */
#define DT195_REG_I2C_TRIGGER 0x800C0u /* kick an I2C transaction */

/* I2C 8-bit write addresses of the on-board chips (from Initialize). */
#define DT195_I2C_DEMOD_T 0x20 /* TC90522XBG, terrestrial system */
#define DT195_I2C_DEMOD_S 0x22 /* TC90522XBG, satellite system */
#define DT195_I2C_TUNER 0xC0   /* RF tuner behind the demod's I2C gate */

/* Write a 32-bit value to a controller register. Returns 0 on success. */
int dt195_reg_write(dt195_device *dev, uint32_t addr, uint32_t value);

/* Read a 32-bit controller register into *value. Returns 0 on success. */
int dt195_reg_read(dt195_device *dev, uint32_t addr, uint32_t *value);

/* Write one register on an I2C chip: chip 8-bit address, register, value.
 * Drives the controller's I2C bridge and checks the ACK. */
int dt195_i2c_write(dt195_device *dev, uint8_t chip_addr, uint8_t reg,
                    uint8_t value);

/* Read one register from an I2C chip into *value. */
int dt195_i2c_read(dt195_device *dev, uint8_t chip_addr, uint8_t reg,
                   uint8_t *value);

#endif /* DT195_I2C_H */
