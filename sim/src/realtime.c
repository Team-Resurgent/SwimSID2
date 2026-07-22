/* realtime.c - Real-time playback backend (see realtime.h).
 *
 * Wraps miniaudio (native WASAPI on Windows). The heavy single-header
 * implementation is compiled here only. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "realtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- live streaming */
struct realtime_stream {
    ma_device    device;
    ma_pcm_rb    rb;
    uint32_t     rate;
    uint32_t     prime;     /* frames to buffer before starting the device */
    int          started;
    int          have_device;
    volatile int aborted;   /* set from any thread to unblock/stop */
};

static void stream_cb(ma_device *dev, void *out, const void *in, ma_uint32 count) {
    realtime_stream *s = (realtime_stream *)dev->pUserData;
    int16_t *o = (int16_t *)out;
    ma_uint32 done = 0;
    (void)in;

    while (done < count) {
        ma_uint32 n = count - done;
        void *buf;
        if (ma_pcm_rb_acquire_read(&s->rb, &n, &buf) != MA_SUCCESS || n == 0)
            break;
        memcpy(o + done, buf, (size_t)n * sizeof(int16_t));
        ma_pcm_rb_commit_read(&s->rb, n);
        done += n;
    }
    for (; done < count; done++)   /* underrun -> silence */
        o[done] = 0;
}

realtime_stream *realtime_stream_open(uint32_t rate) {
    realtime_stream *s = (realtime_stream *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->rate  = rate;
    s->prime = rate / 10;                 /* ~100 ms cushion before playback */

    /* ~1 s ring buffer: plenty of slack given a >real-time producer. */
    if (ma_pcm_rb_init(ma_format_s16, 1, rate, NULL, NULL, &s->rb) != MA_SUCCESS) {
        fprintf(stderr, "realtime: failed to init ring buffer\n");
        free(s);
        return NULL;
    }

    ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate        = rate;
    cfg.dataCallback      = stream_cb;
    cfg.pUserData         = s;

    if (ma_device_init(NULL, &cfg, &s->device) != MA_SUCCESS) {
        fprintf(stderr, "realtime: failed to init audio device\n");
        ma_pcm_rb_uninit(&s->rb);
        free(s);
        return NULL;
    }
    s->have_device = 1;
    return s;
}

static void stream_ensure_started(realtime_stream *s) {
    if (!s->started) {
        if (ma_device_start(&s->device) != MA_SUCCESS)
            fprintf(stderr, "realtime: failed to start audio device\n");
        s->started = 1;
    }
}

void realtime_stream_push(realtime_stream *s, int16_t sample) {
    if (!s)
        return;

    while (!s->aborted) {
        ma_uint32 n = 1;
        void *buf;
        if (ma_pcm_rb_acquire_write(&s->rb, &n, &buf) == MA_SUCCESS && n >= 1) {
            *(int16_t *)buf = sample;
            ma_pcm_rb_commit_write(&s->rb, 1);
            if (!s->started && ma_pcm_rb_available_read(&s->rb) >= s->prime)
                stream_ensure_started(s);
            return;
        }
        /* Ring full: the device must be running to drain it. Wait for room,
         * which paces this (faster-than-real-time) producer to real time. */
        stream_ensure_started(s);
        ma_sleep(2);
    }
}

void realtime_stream_abort(realtime_stream *s) {
    if (s)
        s->aborted = 1;
}

void realtime_stream_close(realtime_stream *s) {
    if (!s)
        return;

    if (s->have_device) {
        if (!s->aborted) {
            stream_ensure_started(s);
            while (ma_pcm_rb_available_read(&s->rb) > 0)   /* drain */
                ma_sleep(10);
            ma_sleep(150);                                 /* flush device buffer */
        }
        ma_device_uninit(&s->device);
    }
    ma_pcm_rb_uninit(&s->rb);
    free(s);
}
