/*
 * dt195_tune.c - terrestrial (ISDB-T) tuning for the PIX-DT195.
 *
 * Topology (reconstructed from TunerManager::Initialize):
 *   TC90522XBG demodulator at I2C 0x20 (terrestrial system), with the
 *   MaxLinear MxL661 RF tuner hanging off the demod's I2C gate at 0xC0.
 *
 * The register programming below follows the public TC90522 + MxL661 ISDB-T
 * model (the same chipset used by the PLEX px4 family). Sequences that have
 * not been confirmed against a bus trace of this specific device are marked
 * VERIFY. No conditional-access/descrambling is performed; tuning only selects
 * the RF channel and hands the raw TS to the caller.
 */
#include "dt195_i2c.h"

#include <errno.h>
#include <stdio.h>

/* Japanese terrestrial UHF: physical channels 13..62, 6 MHz spacing,
 * channel 13 centred at 473.142857 MHz. Returns centre frequency in Hz. */
uint32_t dt195_isdbt_channel_to_hz(int physical_channel)
{
    if (physical_channel < 13 || physical_channel > 62)
        return 0;
    /* 473 1/7 MHz + (ch-13)*6 MHz; keep the 1/7 MHz offset in Hz. */
    return 473142857u + (uint32_t)(physical_channel - 13) * 6000000u;
}

/*
 * Open the TC90522's I2C repeater gate so that writes addressed to the tuner
 * (0xC0) are forwarded onto the tuner's private I2C bus behind the demod.
 * TC90522 controls the gate through its register 0x08 on the system block.
 * VERIFY: gate register/bit for this board.
 */
static int demod_gate(dt195_device *dev, int open)
{
    return dt195_i2c_write(dev, DT195_I2C_DEMOD_T, 0x08, open ? 0x01 : 0x00);
}

/*
 * Bring the terrestrial demod up: soft reset, select ISDB-T, route the TS to
 * the serial/parallel output the controller reads. VERIFY register values.
 */
static int demod_init_t(dt195_device *dev)
{
    int r;
    /* Soft reset the system block. */
    if ((r = dt195_i2c_write(dev, DT195_I2C_DEMOD_T, 0x01, 0x40)))
        return r;
    /* Select ISDB-T demodulation mode. VERIFY. */
    if ((r = dt195_i2c_write(dev, DT195_I2C_DEMOD_T, 0x00, 0x10)))
        return r;
    /* Enable TS output. VERIFY. */
    if ((r = dt195_i2c_write(dev, DT195_I2C_DEMOD_T, 0x1C, 0x00)))
        return r;
    return 0;
}

/*
 * Program the MxL661 to the given RF centre frequency (Hz) for a 6 MHz
 * ISDB-T channel. The MxL661 is driven over the demod's I2C gate.
 *
 * The MxL661 uses a register protocol with a frequency expressed as a
 * fractional multiple of its reference clock. The exact coefficient table is
 * large and chip-firmware specific; the structure is in place and the
 * per-register values are the remaining item to confirm against hardware.
 * VERIFY: MxL661 register sequence and frequency coefficients.
 */
static int tuner_set_freq(dt195_device *dev, uint32_t hz)
{
    int r;
    if ((r = demod_gate(dev, 1)))
        return r;

    /* Channel bandwidth = 6 MHz (ISDB-T). VERIFY register. */
    r = dt195_i2c_write(dev, DT195_I2C_TUNER, 0x00, 0x06);

    /* Frequency, in the MxL661's units (kHz split across registers). The
     * placeholder below writes the frequency in kHz big-endian; the real chip
     * wants its fractional-N encoding. TODO: encode per MxL661 programming. */
    if (r == 0) {
        uint32_t khz = hz / 1000u;
        r = dt195_i2c_write(dev, DT195_I2C_TUNER, 0x01, (uint8_t)(khz >> 16));
        if (r == 0)
            r = dt195_i2c_write(dev, DT195_I2C_TUNER, 0x02, (uint8_t)(khz >> 8));
        if (r == 0)
            r = dt195_i2c_write(dev, DT195_I2C_TUNER, 0x03, (uint8_t)(khz));
    }

    int g = demod_gate(dev, 0);
    return r ? r : g;
}

/*
 * Public entry used by dt195_select_channel(): bring the terrestrial path up
 * and tune to `frequency` (Hz). ts_id/service_id selection happens later in
 * the stream path and is accepted but not yet used here.
 */
int dt195_tune_terrestrial(dt195_device *dev, uint32_t frequency,
                           uint16_t ts_id, uint16_t service_id)
{
    (void)ts_id;
    (void)service_id;
    int r = demod_init_t(dev);
    if (r != 0) {
        fprintf(stderr, "dt195: terrestrial demod init failed: %s\n",
                dt195_strerror(r));
        return r;
    }
    r = tuner_set_freq(dev, frequency);
    if (r != 0) {
        fprintf(stderr, "dt195: tuner set-freq failed: %s\n",
                dt195_strerror(r));
        return r;
    }
    return 0;
}
