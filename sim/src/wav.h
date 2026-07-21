/* wav.h - Minimal 16-bit PCM mono/stereo WAV writer. */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE    *f;
    uint32_t rate;
    int      channels;
    uint32_t nframes;   /* number of sample frames written */
} wav_writer_t;

/* Open a WAV file for writing. Returns 0 on success. */
int  wav_open(wav_writer_t *w, const char *path, uint32_t rate, int channels);
/* Write one signed 16-bit sample (call once per channel per frame). */
void wav_write(wav_writer_t *w, int16_t sample);
/* Patch the header sizes and close. */
void wav_close(wav_writer_t *w);

#endif /* WAV_H */
