/* realtime.c - Real-time playback backend (see realtime.h).
 *
 * Wraps miniaudio (native WASAPI on Windows). The heavy single-header
 * implementation is compiled here only. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "realtime.h"
#include <stdio.h>

typedef struct {
    const int16_t *data;
    size_t         frames;
    size_t         pos;
} playbuf_t;

static void data_cb(ma_device *dev, void *out, const void *in, ma_uint32 count) {
    playbuf_t *pb = (playbuf_t *)dev->pUserData;
    int16_t   *o  = (int16_t *)out;
    (void)in;
    for (ma_uint32 i = 0; i < count; i++)
        o[i] = (pb->pos < pb->frames) ? pb->data[pb->pos++] : 0;
}

int realtime_play(const int16_t *samples, size_t nframes, uint32_t rate) {
    playbuf_t pb = { samples, nframes, 0 };

    ma_device_config cfg   = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format    = ma_format_s16;
    cfg.playback.channels  = 1;
    cfg.sampleRate         = rate;
    cfg.dataCallback       = data_cb;
    cfg.pUserData          = &pb;

    ma_device device;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS) {
        fprintf(stderr, "realtime: failed to init audio device\n");
        return -1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "realtime: failed to start audio device\n");
        ma_device_uninit(&device);
        return -1;
    }

    while (pb.pos < pb.frames)
        ma_sleep(30);
    ma_sleep(250);   /* let the final buffer drain */

    ma_device_uninit(&device);
    return 0;
}
