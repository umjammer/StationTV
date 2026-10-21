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

static int cmd_capture(const char *out, int channel)
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

    uint32_t hz = dt195_isdbt_channel_to_hz(channel);
    if (hz == 0) {
        fprintf(stderr, "invalid terrestrial channel %d (expected 13..62)\n",
                channel);
        dt195_close(dev);
        return 1;
    }
    fprintf(stderr, "tuning terrestrial ch %d (%u Hz)...\n", channel, hz);
    dt195_select_channel(dev, DT195_WAVE_TERRESTRIAL, hz, 0, 0);

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
    if (argc >= 4 && strcmp(argv[1], "capture") == 0)
        return cmd_capture(argv[2], atoi(argv[3]));

    fprintf(stderr,
            "usage:\n"
            "  %s info                   enumerate the device\n"
            "  %s capture OUT.ts CHAN    tune terrestrial channel CHAN (13..62)\n"
            "                            and dump the raw transport stream\n",
            argv[0], argv[0]);
    return 2;
}
