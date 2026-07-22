/* wav.c - Minimal 16-bit PCM WAV writer (see wav.h). */
#include "wav.h"
#include <string.h>

static void put32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static void put16(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

int wav_open(wav_writer_t *w, const char *path, uint32_t rate, int channels) {
    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    w->rate = rate;
    w->channels = channels;
    w->nframes = 0;

    uint32_t byte_rate = rate * channels * 2;
    uint16_t block_align = (uint16_t)(channels * 2);

    /* RIFF header with placeholder sizes (patched in wav_close). */
    fwrite("RIFF", 1, 4, w->f);
    put32(w->f, 0);                 /* file size - 8 */
    fwrite("WAVE", 1, 4, w->f);
    fwrite("fmt ", 1, 4, w->f);
    put32(w->f, 16);                /* fmt chunk size */
    put16(w->f, 1);                 /* PCM */
    put16(w->f, (uint16_t)channels);
    put32(w->f, rate);
    put32(w->f, byte_rate);
    put16(w->f, block_align);
    put16(w->f, 16);                /* bits per sample */
    fwrite("data", 1, 4, w->f);
    put32(w->f, 0);                 /* data size */
    return 0;
}

void wav_write(wav_writer_t *w, int16_t sample) {
    if (!w->f) return;
    put16(w->f, (uint16_t)sample);
    /* nframes counts sample frames; caller writes 'channels' samples per frame */
}

void wav_close(wav_writer_t *w) {
    if (!w->f) return;
    long end = ftell(w->f);
    uint32_t data_bytes = (uint32_t)(end - 44);

    fseek(w->f, 4, SEEK_SET);
    put32(w->f, 36 + data_bytes);   /* RIFF size */
    fseek(w->f, 40, SEEK_SET);
    put32(w->f, data_bytes);        /* data size */

    fclose(w->f);
    w->f = NULL;
}
