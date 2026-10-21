/*
 * main.c - CLI front-end for the PIX-DT195 research driver.
 *
 *   dt195 info             enumerate the device and show endpoints/firmware
 *   dt195 capture out.ts   bring the controller up and dump the raw TS
 */
#include "dt195.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile bool g_stop = false;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = true;
}

static int cmd_info(void)
{
    dt195_device *dev = NULL;
    int r = dt195_open(&dev);
    if (r != 0) {
        fprintf(stderr, "open failed: %s\n", dt195_strerror(r));
        return 1;
    }
    printf("PIX-DT195 found (%04X:%04X)\n", DT195_VID, DT195_PID);
    printf("bulk endpoints: %d\n", dt195_endpoint_count(dev));
    dt195_close(dev);
    return 0;
}

static int cmd_capture(const char *out)
{
    dt195_device *dev = NULL;
    int r = dt195_open(&dev);
    if (r != 0) {
        fprintf(stderr, "open failed: %s\n", dt195_strerror(r));
        return 1;
    }

    r = dt195_upload_firmware(dev, NULL);
    if (r != 0) {
        fprintf(stderr, "firmware upload failed: %s\n", dt195_strerror(r));
        dt195_close(dev);
        return 1;
    }

    /* Tuning is not wired up yet; attempt it so the pipeline is exercised
     * and the capture loop still runs (it will idle until a stream appears). */
    dt195_select_channel(dev, DT195_WAVE_TERRESTRIAL, 0, 0, 0);

    FILE *fp = fopen(out, "wb");
    if (!fp) {
        perror("fopen");
        dt195_close(dev);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    fprintf(stderr, "capturing to %s (Ctrl-C to stop)...\n", out);

    r = dt195_stream_raw(dev, fp, &g_stop);
    fclose(fp);
    dt195_close(dev);

    if (r != 0) {
        fprintf(stderr, "capture ended: %s\n", dt195_strerror(r));
        return 1;
    }
    fprintf(stderr, "stopped.\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "info") == 0)
        return cmd_info();
    if (argc >= 3 && strcmp(argv[1], "capture") == 0)
        return cmd_capture(argv[2]);

    fprintf(stderr,
            "usage:\n"
            "  %s info             enumerate the device\n"
            "  %s capture OUT.ts   dump the raw transport stream\n",
            argv[0], argv[0]);
    return 2;
}
