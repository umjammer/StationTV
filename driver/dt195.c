/*
 * dt195.c - PIX-DT195 user-space driver implementation (libusb).
 *
 * See dt195.h for the public API and README.md for how this maps onto the
 * shipped DT195Lib.framework. No conditional-access / descrambling logic.
 */
#include "dt195.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

/* Vendor control command values reconstructed from the shipped stack.
 * The register-load address below is the one Felis firmware upload writes to
 * (SeleneConnectionManager::downloadIdleFirwmware, "Set ... Download Command").
 * Request codes are placeholders pending further tracing; see TODO. */
#define DT195_REG_FW_LOAD 0x90074u
#define DT195_CTRL_OUT (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | \
                        LIBUSB_RECIPIENT_DEVICE)
#define DT195_CTRL_IN  (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | \
                        LIBUSB_RECIPIENT_DEVICE)

struct ep {
    uint8_t addr;   /* endpoint address incl. direction bit */
    bool in;        /* true for IN endpoint */
};

struct dt195_device {
    libusb_context *ctx;
    libusb_device_handle *handle;
    int iface;
    struct ep bulk[8];
    int nbulk;
    int bulk_in;  /* index into bulk[] of the TS/data IN pipe, or -1 */
    int bulk_out; /* index into bulk[] of the command OUT pipe, or -1 */
};

const char *dt195_strerror(int err)
{
    if (err >= 0)
        return "ok";
    if (err == -ENOENT)
        return "device not found";
    if (err == -EIO)
        return "I/O error";
    return libusb_strerror((enum libusb_error)err);
}

static int find_bulk_endpoints(dt195_device *d, libusb_device *dev)
{
    struct libusb_config_descriptor *cfg = NULL;
    int r = libusb_get_active_config_descriptor(dev, &cfg);
    if (r != 0)
        return r;

    d->nbulk = 0;
    d->bulk_in = d->bulk_out = -1;
    d->iface = 0;

    for (int i = 0; i < cfg->bNumInterfaces && d->nbulk < 8; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int a = 0; a < itf->num_altsetting; a++) {
            const struct libusb_interface_descriptor *id = &itf->altsetting[a];
            for (int e = 0; e < id->bNumEndpoints && d->nbulk < 8; e++) {
                const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
                    LIBUSB_TRANSFER_TYPE_BULK)
                    continue;
                bool in = (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
                d->bulk[d->nbulk].addr = ep->bEndpointAddress;
                d->bulk[d->nbulk].in = in;
                if (in && d->bulk_in < 0)
                    d->bulk_in = d->nbulk;
                if (!in && d->bulk_out < 0)
                    d->bulk_out = d->nbulk;
                d->nbulk++;
            }
            if (id->bNumEndpoints)
                d->iface = id->bInterfaceNumber;
        }
    }
    libusb_free_config_descriptor(cfg);
    return 0;
}

int dt195_open(dt195_device **out)
{
    if (!out)
        return -EINVAL;
    *out = NULL;

    dt195_device *d = calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;

    int r = libusb_init(&d->ctx);
    if (r != 0) {
        free(d);
        return r;
    }

    d->handle = libusb_open_device_with_vid_pid(d->ctx, DT195_VID, DT195_PID);
    if (!d->handle) {
        libusb_exit(d->ctx);
        free(d);
        return -ENOENT;
    }

    libusb_device *dev = libusb_get_device(d->handle);
    r = find_bulk_endpoints(d, dev);
    if (r != 0)
        goto fail;

    /* macOS: no kernel driver normally owns this device, but detach anyway. */
    libusb_set_auto_detach_kernel_driver(d->handle, 1);

    int cfg = 0;
    if (libusb_get_configuration(d->handle, &cfg) == 0 && cfg != 1)
        libusb_set_configuration(d->handle, 1);

    r = libusb_claim_interface(d->handle, d->iface);
    if (r != 0)
        goto fail;

    *out = d;
    return 0;

fail:
    libusb_close(d->handle);
    libusb_exit(d->ctx);
    free(d);
    return r;
}

void dt195_close(dt195_device *d)
{
    if (!d)
        return;
    if (d->handle) {
        libusb_release_interface(d->handle, d->iface);
        libusb_close(d->handle);
    }
    if (d->ctx)
        libusb_exit(d->ctx);
    free(d);
}

int dt195_endpoint_count(const dt195_device *d)
{
    return d ? d->nbulk : 0;
}

int dt195_control(dt195_device *d, uint8_t bm_request_type, uint8_t b_request,
                  uint16_t w_value, uint16_t w_index, uint8_t *data,
                  uint16_t w_length, unsigned timeout_ms)
{
    if (!d || !d->handle)
        return -EINVAL;
    return libusb_control_transfer(d->handle, bm_request_type, b_request,
                                   w_value, w_index, data, w_length,
                                   timeout_ms);
}

int dt195_bulk_write(dt195_device *d, int pipe, const uint8_t *buf, int len,
                     unsigned timeout_ms)
{
    if (!d || !d->handle || pipe < 0 || pipe >= d->nbulk)
        return -EINVAL;
    int got = 0;
    int r = libusb_bulk_transfer(d->handle, d->bulk[pipe].addr, (uint8_t *)buf,
                                 len, &got, timeout_ms);
    return r == 0 ? got : r;
}

int dt195_bulk_read(dt195_device *d, int pipe, uint8_t *buf, int len,
                    int *transferred, unsigned timeout_ms)
{
    if (!d || !d->handle || pipe < 0 || pipe >= d->nbulk)
        return -EINVAL;
    int got = 0;
    int r = libusb_bulk_transfer(d->handle, d->bulk[pipe].addr, buf, len, &got,
                                 timeout_ms);
    if (transferred)
        *transferred = got;
    /* A timeout with partial data is normal for a streaming endpoint. */
    if (r == LIBUSB_ERROR_TIMEOUT && got > 0)
        return got;
    return r == 0 ? got : r;
}

int dt195_upload_firmware(dt195_device *d, const char *path)
{
    if (!d || !d->handle)
        return -EINVAL;
    if (d->bulk_out < 0)
        return -ENOENT; /* no OUT pipe to load firmware through */
    if (!path)
        path = DT195_FW_IDLE;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "dt195: cannot open firmware '%s': %s\n", path,
                strerror(errno));
        return -ENOENT;
    }

    /* Step 1: force-reset the controller before loading.
     * TODO: confirm the exact vendor request code; DT195Lib issues a
     * "CMD1: Force Reset Command" here and waits on the interrupt endpoint. */
    uint8_t reset = 0x03; /* reset selector seen passed to the reset command */
    dt195_control(d, DT195_CTRL_OUT, 0x01 /* TODO request */, 0, 0, &reset, 1,
                  1000);

    /* Step 2: point the controller at its firmware-load register.
     * DT195Lib writes the load address 0x90074 before streaming. */
    uint8_t addr[4] = {
        (uint8_t)(DT195_REG_FW_LOAD >> 24), (uint8_t)(DT195_REG_FW_LOAD >> 16),
        (uint8_t)(DT195_REG_FW_LOAD >> 8), (uint8_t)(DT195_REG_FW_LOAD)};
    dt195_control(d, DT195_CTRL_OUT, 0x02 /* TODO request */, 0, 0, addr,
                  sizeof(addr), 1000);

    /* Step 3: stream the blob in DT195_FW_CHUNK pieces to the OUT pipe. */
    uint8_t chunk[DT195_FW_CHUNK];
    size_t total = 0;
    int rc = 0;
    for (;;) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n == 0)
            break;
        int w = dt195_bulk_write(d, d->bulk_out, chunk, (int)n, 2000);
        if (w < 0) {
            rc = w;
            break;
        }
        total += (size_t)w;
    }
    fclose(fp);

    if (rc != 0) {
        fprintf(stderr, "dt195: firmware upload failed after %zu bytes: %s\n",
                total, dt195_strerror(rc));
        return rc;
    }
    fprintf(stderr, "dt195: uploaded %zu bytes of firmware\n", total);

    /* Step 4: wait for the chip to report the Idle firmware state.
     * TODO: read the firmware-state register and poll until "Idle". */
    return 0;
}

int dt195_select_channel(dt195_device *d, dt195_wave_t wave, uint32_t frequency,
                         uint16_t ts_id, uint16_t service_id)
{
    if (!d || !d->handle)
        return -EINVAL;
    (void)wave;
    (void)frequency;
    (void)ts_id;
    (void)service_id;
    /* TODO: program the RF front-end (MxL661 / CXD2832AWR) over the
     * controller's I2C bus, then select the transport stream. This requires
     * the per-chip register tables from TunerManager / MxL661Manager /
     * CXD2832AWRManager, which are the next research target. */
    fprintf(stderr, "dt195: select_channel not yet implemented "
                    "(RF tuning tables pending)\n");
    return -ENOSYS;
}

int dt195_stream_raw(dt195_device *d, void *fp_, const volatile bool *stop)
{
    if (!d || !d->handle || !fp_)
        return -EINVAL;
    if (d->bulk_in < 0)
        return -ENOENT;
    FILE *fp = (FILE *)fp_;

    /* TODO: issue startSys1Recv (tell the firmware to push the raw TS) before
     * draining. Without tuning + startSys1Recv the IN pipe will simply time
     * out, which is reported cleanly below. */

    enum { BUFSZ = 188 * 348 }; /* ~64 KiB, whole TS packets */
    uint8_t *buf = malloc(BUFSZ);
    if (!buf)
        return -ENOMEM;

    int rc = 0;
    while (!(stop && *stop)) {
        int got = 0;
        int r = dt195_bulk_read(d, d->bulk_in, buf, BUFSZ, &got, 1000);
        if (r == LIBUSB_ERROR_TIMEOUT) {
            if (got <= 0)
                continue; /* keep waiting for the stream */
        } else if (r < 0) {
            rc = r;
            break;
        }
        if (got > 0 && fwrite(buf, 1, (size_t)got, fp) != (size_t)got) {
            rc = -EIO;
            break;
        }
    }
    free(buf);
    return rc;
}
