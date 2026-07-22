/*
 * swinsid_cli.c - Thin command-line front-end over swinsid.dll.
 *
 * Usage (firmware, the default):
 *   swinsid_sim <firmware.elf> <tune.sid> [out.wav] [options]
 * Usage (reSIDfp reference, no firmware needed):
 *   swinsid_sim --reference <tune.sid> [out.wav] [options]
 *
 *   options: [--song N] [--seconds S] [--rate R] [--6581] [--8580] [--play]
 *
 * Without --play a WAV path is required and the tune is rendered for --seconds.
 * With --play the whole tune streams live to the default device until Ctrl-C
 * (--seconds is ignored for playback). Ctrl-C stops the active render/play.
 * --reference drives the reSIDfp cycle-exact SID (the "real chip" reference)
 * instead of the SwinSID firmware, for A/B comparison.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "swinsid.h"

static void log_line(const char *line, void *user) {
    (void)user;
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void on_sigint(int sig) {
    (void)sig;
    swinsid_stop();
}

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s <firmware.elf> <tune.sid> [out.wav] [options]   (SwinSID firmware)\n"
        "       %s --reference <tune.sid> [out.wav] [options]      (reSIDfp reference)\n"
        "  options: [--song N] [--seconds S] [--rate R] [--6581] [--8580] [--play]\n"
        "  --play streams the whole tune until Ctrl-C; --seconds bounds render only.\n", p, p);
}

int main(int argc, char **argv) {
    const char *pos[3] = { NULL, NULL, NULL };
    int npos = 0;
    int play = 0;
    int reference = 0;

    swinsid_options opt;
    swinsid_default_options(&opt);
    opt.seconds = 180.0;   /* render default (ignored for --play) */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--song") && i + 1 < argc)         opt.song = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) opt.seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc)    opt.rate = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--6581"))                    opt.filter8580 = 0;
        else if (!strcmp(argv[i], "--8580"))                    opt.filter8580 = 1;
        else if (!strcmp(argv[i], "--play"))                    play = 1;
        else if (!strcmp(argv[i], "--reference") ||
                 !strcmp(argv[i], "--ref"))                     reference = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else if (npos < 3) {
            pos[npos++] = argv[i];
        }
    }

    /* Reference mode needs no firmware: positionals are <tune.sid> [out.wav]. */
    const char *elf_path, *sid_path, *wav_path;
    if (reference) {
        if (npos < 1) { usage(argv[0]); return 1; }
        elf_path = NULL;
        sid_path = pos[0];
        wav_path = pos[1];
    } else {
        if (npos < 2) { usage(argv[0]); return 1; }
        elf_path = pos[0];
        sid_path = pos[1];
        wav_path = pos[2];
    }

    if (!play && !wav_path) {
        fprintf(stderr, "error: a WAV output path is required for rendering "
                        "(or pass --play to stream live)\n");
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, on_sigint);

    if (reference)
        return play
            ? swinsid_ref_play(sid_path, &opt, log_line, NULL)
            : swinsid_ref_render(sid_path, wav_path, &opt, log_line, NULL);
    return play
        ? swinsid_play(elf_path, sid_path, &opt, log_line, NULL)
        : swinsid_render(elf_path, sid_path, wav_path, &opt, log_line, NULL);
}
