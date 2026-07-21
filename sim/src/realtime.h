/* realtime.h - Real-time speaker playback of rendered PCM (via miniaudio). */
#ifndef REALTIME_H
#define REALTIME_H

#include <stdint.h>
#include <stddef.h>

/*
 * Play 'nframes' signed 16-bit mono samples through the default output device
 * at 'rate' Hz, blocking until playback finishes. Returns 0 on success.
 */
int realtime_play(const int16_t *samples, size_t nframes, uint32_t rate);

#endif /* REALTIME_H */
