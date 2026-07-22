/* realtime.h - Real-time speaker playback via miniaudio.
 *
 * A live producer/consumer stream: the simulation pushes samples as it
 * generates them; playback starts almost immediately and the push call paces
 * the producer to real time, so stopping the caller stops audio at once.
 */
#ifndef REALTIME_H
#define REALTIME_H

#include <stdint.h>
#include <stddef.h>

/* Opaque live audio stream. */
typedef struct realtime_stream realtime_stream;

/*
 * Open a live mono s16 playback stream at 'rate' Hz. Returns NULL on failure.
 * The output device is started lazily once enough samples are buffered.
 */
realtime_stream *realtime_stream_open(uint32_t rate);

/*
 * Push one sample into the stream. Blocks (pacing the caller to real time)
 * while the ring buffer is full, so a faster-than-real-time producer does not
 * run ahead. Returns immediately without blocking once the stream has been
 * aborted. Safe to call with a NULL stream (no-op).
 */
void realtime_stream_push(realtime_stream *s, int16_t sample);

/*
 * Request an immediate stop: unblocks any in-flight push and makes
 * realtime_stream_close() skip draining. Thread-safe; NULL-safe.
 */
void realtime_stream_abort(realtime_stream *s);

/* Drain any buffered audio (unless aborted), stop the device, free the stream. */
void realtime_stream_close(realtime_stream *s);

#endif /* REALTIME_H */
