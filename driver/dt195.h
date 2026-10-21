/*
 * dt195.h - user-space driver for the PIXELA PIX-DT195 USB TV tuner.
 *
 * Research / interoperability scaffold for current macOS (arm64) via libusb.
 * Reconstructed from the USB interface of the shipped DT195Lib.framework.
 *
 * Deliberately contains no conditional-access (B-CAS/ARIB CAS) or
 * content-protection logic. It brings the controller up and reads the raw
 * broadcast transport stream; it does not descramble anything.
 */
#ifndef DT195_H
#define DT195_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB identity, as matched by DT195Lib's device-discovery thread. */
#define DT195_VID 0x06B8 /* PIXELA Corporation */
#define DT195_PID 0x103A

/* Controller firmware uploaded to the MB86M01 "Felis" at start-up.
 * Proprietary; see extract-firmware.sh. Default location: ./firmware/. */
#define DT195_FW_IDLE "firmware/mb86m01_assp_osec_idle.bin"

/* Firmware is streamed to the controller in chunks of this size
 * (SeleneConnectionManager::downloadIdleFirwmware reads 0x1000 at a time). */
#define DT195_FW_CHUNK 4096

/* Broadcast system selected by dt195_select_channel(), matching eWave_t. */
typedef enum {
    DT195_WAVE_TERRESTRIAL = 0, /* 地上デジタル */
    DT195_WAVE_BS = 1,
    DT195_WAVE_CS = 2,
} dt195_wave_t;

typedef struct dt195_device dt195_device;

/* Return a short human-readable string for a libusb/driver error code. */
const char *dt195_strerror(int err);

/* Open the first PIX-DT195 on the bus, set configuration, and claim its
 * interface. On success *out receives an owned handle; free with
 * dt195_close(). Returns 0 or a negative error code. */
int dt195_open(dt195_device **out);

/* Release the interface and the device. Safe on NULL. */
void dt195_close(dt195_device *dev);

/* Number of bulk IN/OUT endpoints discovered on the claimed interface. */
int dt195_endpoint_count(const dt195_device *dev);

/* Upload the idle controller firmware from `path` (NULL -> DT195_FW_IDLE):
 * force-reset the controller, stream the blob in DT195_FW_CHUNK pieces, and
 * wait for the chip to report the Idle firmware state. Returns 0 on success. */
int dt195_upload_firmware(dt195_device *dev, const char *path);

/* Tune. Programs the RF front-end and selects the transport stream.
 * `frequency` is the centre frequency in Hz (or the channel's physical
 * frequency); `ts_id`/`service_id` pick the stream/service. */
int dt195_select_channel(dt195_device *dev, dt195_wave_t wave,
                         uint32_t frequency, uint16_t ts_id,
                         uint16_t service_id);

/* Read the raw broadcast TS ("Sys1" path) and append it to `fp` until
 * `stop` becomes true or an error occurs. `fp` should be a binary stream.
 * Returns 0 on a clean stop, or a negative error code. */
int dt195_stream_raw(dt195_device *dev, void *fp, const volatile bool *stop);

/* --- lower-level primitives (exposed for research) --- */

/* Vendor control transfer (maps to USBConnectionManagerMacImpl::usbWriteControl). */
int dt195_control(dt195_device *dev, uint8_t bm_request_type, uint8_t b_request,
                  uint16_t w_value, uint16_t w_index, uint8_t *data,
                  uint16_t w_length, unsigned timeout_ms);

/* Bulk write / read on a discovered pipe index. */
int dt195_bulk_write(dt195_device *dev, int pipe, const uint8_t *buf, int len,
                     unsigned timeout_ms);
int dt195_bulk_read(dt195_device *dev, int pipe, uint8_t *buf, int len,
                    int *transferred, unsigned timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* DT195_H */
