/*
 * dt195_i2c.c - controller register + I2C bridge implementation.
 *
 * What is reconstructed from the binary (concrete):
 *   - register access uses vendor request 0xBC with the 32-bit address sent
 *     big-endian (ControlRegisterWrite);
 *   - I2C transactions are framed through the window register 0x823BE and the
 *     trigger register 0x800C0, with the result ACK read back from 0x823BE
 *     (ControlWriteI2c).
 *
 * What still needs confirming on real hardware is marked VERIFY: the exact
 * byte layout written into the I2C window, and the trigger value. The shape
 * below follows the observed "write address+reg+data, trigger, read status"
 * sequence.
 */
#include "dt195_i2c.h"

#include <errno.h>

/* bRequest 0xBC carries the register address in wValue:wIndex (big-endian
 * high half in wValue, low half in wIndex) and the 32-bit payload as data. */
int dt195_reg_write(dt195_device *dev, uint32_t addr, uint32_t value)
{
    uint8_t payload[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)(value)};
    int r = dt195_control(dev, 0x40, DT195_VREQ_REG,
                          (uint16_t)(addr >> 16), (uint16_t)(addr & 0xFFFF),
                          payload, sizeof(payload), 1000);
    return r < 0 ? r : 0;
}

int dt195_reg_read(dt195_device *dev, uint32_t addr, uint32_t *value)
{
    uint8_t payload[4] = {0, 0, 0, 0};
    int r = dt195_control(dev, 0xC0, DT195_VREQ_REG,
                          (uint16_t)(addr >> 16), (uint16_t)(addr & 0xFFFF),
                          payload, sizeof(payload), 1000);
    if (r < 0)
        return r;
    if (value)
        *value = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                 ((uint32_t)payload[2] << 8) | payload[3];
    return 0;
}

/*
 * One I2C register write, driven through the controller's bridge:
 *   1. load the I2C command (chip addr, register, data byte) into the window;
 *   2. pulse the trigger register;
 *   3. read the window back and check the low byte for a NAK.
 * VERIFY: window packing and trigger value against a bus trace.
 */
int dt195_i2c_write(dt195_device *dev, uint8_t chip_addr, uint8_t reg,
                    uint8_t value)
{
    /* Pack addr/reg/data into one 32-bit window word (write bit = addr&~1). */
    uint32_t cmd = ((uint32_t)(chip_addr & 0xFEu) << 16) |
                   ((uint32_t)reg << 8) | value;
    int r = dt195_reg_write(dev, DT195_REG_I2C_WINDOW, cmd);
    if (r != 0)
        return r;

    r = dt195_reg_write(dev, DT195_REG_I2C_TRIGGER, 0x09u); /* I2C xfer cmd */
    if (r != 0)
        return r;

    uint32_t status = 0;
    r = dt195_reg_read(dev, DT195_REG_I2C_WINDOW, &status);
    if (r != 0)
        return r;
    /* A set NAK bit in the status word means the chip did not ACK. */
    if (status & 0x01u)
        return -EIO;
    return 0;
}

int dt195_i2c_read(dt195_device *dev, uint8_t chip_addr, uint8_t reg,
                   uint8_t *value)
{
    /* Point the chip at `reg`, then issue a read (addr | 1). */
    uint32_t cmd = ((uint32_t)(chip_addr & 0xFEu) << 16) | ((uint32_t)reg << 8);
    int r = dt195_reg_write(dev, DT195_REG_I2C_WINDOW, cmd);
    if (r != 0)
        return r;
    r = dt195_reg_write(dev, DT195_REG_I2C_TRIGGER, 0x09u);
    if (r != 0)
        return r;

    uint32_t rd = ((uint32_t)(chip_addr | 0x01u) << 16);
    r = dt195_reg_write(dev, DT195_REG_I2C_WINDOW, rd);
    if (r != 0)
        return r;
    r = dt195_reg_write(dev, DT195_REG_I2C_TRIGGER, 0x09u);
    if (r != 0)
        return r;

    uint32_t status = 0;
    r = dt195_reg_read(dev, DT195_REG_I2C_WINDOW, &status);
    if (r != 0)
        return r;
    if (value)
        *value = (uint8_t)(status & 0xFFu);
    return 0;
}
