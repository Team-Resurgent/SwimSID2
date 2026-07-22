/* wavstat.c - quick sanity check for 16-bit PCM WAV output. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.wav\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 44, SEEK_SET);           /* skip standard 44-byte header */
    long n = 0, nz = 0;
    int mn = 32767, mx = -32768;
    double sumsq = 0;
    int16_t s;
    while (fread(&s, 2, 1, f) == 1) {
        n++;
        if (s) nz++;
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        sumsq += (double)s * s;
    }
    fclose(f);
    double rms = n ? sqrt(sumsq / n) : 0;
    printf("samples=%ld nonzero=%ld min=%d max=%d rms=%.1f\n", n, nz, mn, mx, rms);
    return 0;
}
