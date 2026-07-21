/* sidfile.h - PSID/RSID (v1/v2+) file loader. */
#ifndef SIDFILE_H
#define SIDFILE_H

#include <stdint.h>

typedef struct {
    char     magic[5];      /* "PSID" or "RSID" */
    uint16_t version;
    uint16_t load_addr;     /* resolved actual load address */
    uint16_t init_addr;     /* resolved init address */
    uint16_t play_addr;     /* 0 => IRQ-vector driven (see $0314 after init) */
    uint16_t songs;
    uint16_t start_song;    /* 1-based */
    uint32_t speed;
    uint16_t flags;
    int      is_rsid;
    char     name[33];
    char     author[33];
    char     released[33];
} sid_info_t;

/*
 * Load a PSID/RSID file at 'path' into the 64 KiB memory image 'mem'
 * (mem must point to a 65536-byte buffer). Fills 'info'.
 * Returns 0 on success, non-zero on error.
 */
int sid_load(const char *path, uint8_t *mem, sid_info_t *info);

#endif /* SIDFILE_H */
