/*
 * swinsid.h - Public C API for the SwinSID firmware emulator.
 *
 * The engine loads the assembled SwinSID AVR firmware (ELF) into simavr, plays
 * a .sid tune through an emulated C64 6502 CPU, bridges every SID register
 * write onto the emulated C64 bus exactly as the real hardware sees it, and
 * captures the firmware's PWM audio output.
 *
 * Two entry points are offered:
 *   - swinsid_render(): write the captured audio to a WAV file.
 *   - swinsid_play():   stream the captured audio live to the default device.
 *
 * Both block until the requested duration elapses or swinsid_stop() is called
 * (swinsid_stop is safe to call from another thread). Only one render/play may
 * be active at a time.
 */
#ifndef SWINSID_H
#define SWINSID_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SWINSID_BUILD)
#    define SWINSID_API __declspec(dllexport)
#  else
#    define SWINSID_API __declspec(dllimport)
#  endif
#else
#  define SWINSID_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Optional line-oriented logging sink (no trailing newline). */
typedef void (*swinsid_log_fn)(const char *line, void *user);

typedef struct {
    int      song;        /* 1-based sub-song; 0 => the tune's start song */
    double   seconds;     /* duration to render/play                      */
    uint32_t rate;        /* output sample rate in Hz (e.g. 44100)        */
    int      filter8580;  /* 1 = 8580 filter mode, 0 = 6581               */
    int      region;      /* 0 = PAL (default), 1 = NTSC                  */
    int      voice;       /* 0 = full mix (default); 1/2/3 = solo a voice */
    int      match_level; /* 1 = scale firmware down to the reSIDfp line  */
                          /* level so A/B is loudness-matched; 0 = as-is  */
} swinsid_options;

/* Fill 'opt' with defaults (start song, 30 s, 44100 Hz, 8580, PAL). */
SWINSID_API void swinsid_default_options(swinsid_options *opt);

/*
 * Render 'sid_path' (using firmware 'elf_path') to a mono 16-bit WAV at
 * 'wav_path'. Returns 0 on success, non-zero on error or if stopped early.
 */
SWINSID_API int swinsid_render(const char *elf_path, const char *sid_path,
                               const char *wav_path, const swinsid_options *opt,
                               swinsid_log_fn log, void *log_user);

/*
 * Play 'sid_path' (using firmware 'elf_path') live through the default output
 * device. Returns 0 on success, non-zero on error or if stopped early.
 */
SWINSID_API int swinsid_play(const char *elf_path, const char *sid_path,
                             const swinsid_options *opt,
                             swinsid_log_fn log, void *log_user);

/*
 * Reference engine: the same tune driven through the reSIDfp cycle-accurate
 * SID emulation instead of the SwinSID firmware, for A/B comparison. These use
 * the identical 6502/PSID player and register-write stream, so the difference
 * heard is the SID chip emulation. No firmware ELF is needed.
 */
SWINSID_API int swinsid_ref_render(const char *sid_path, const char *wav_path,
                                   const swinsid_options *opt,
                                   swinsid_log_fn log, void *log_user);

SWINSID_API int swinsid_ref_play(const char *sid_path, const swinsid_options *opt,
                                 swinsid_log_fn log, void *log_user);

/*
 * Request the active render/play to stop as soon as possible. Thread-safe and
 * a no-op if nothing is running.
 */
SWINSID_API void swinsid_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* SWINSID_H */
