/* sidfile.c - PSID/RSID file loader (see sidfile.h). */
#include "sidfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void copy_str(char *dst, const uint8_t *src, int n) {
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int sid_load(const char *path, uint8_t *mem, sid_info_t *info) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "sid_load: cannot open %s\n", path); return -1; }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0x7c) { fprintf(stderr, "sid_load: file too small\n"); fclose(f); return -1; }

    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return -1; }
    fclose(f);

    if (memcmp(buf, "PSID", 4) != 0 && memcmp(buf, "RSID", 4) != 0) {
        fprintf(stderr, "sid_load: not a PSID/RSID file\n");
        free(buf);
        return -1;
    }

    memset(info, 0, sizeof(*info));
    memcpy(info->magic, buf, 4);
    info->magic[4]  = '\0';
    info->is_rsid   = (memcmp(buf, "RSID", 4) == 0);
    info->version   = be16(buf + 0x04);
    uint16_t data_off = be16(buf + 0x06);
    uint16_t load_hdr = be16(buf + 0x08);
    info->init_addr = be16(buf + 0x0a);
    info->play_addr = be16(buf + 0x0c);
    info->songs     = be16(buf + 0x0e);
    info->start_song= be16(buf + 0x10);
    info->speed     = be32(buf + 0x12);
    copy_str(info->name,     buf + 0x16, 32);
    copy_str(info->author,   buf + 0x36, 32);
    copy_str(info->released, buf + 0x56, 32);
    if (info->version >= 2 && len >= 0x78)
        info->flags = be16(buf + 0x76);

    if (data_off > len) { free(buf); return -1; }
    const uint8_t *data = buf + data_off;
    long datalen = len - data_off;

    uint16_t load;
    if (load_hdr == 0) {
        if (datalen < 2) { free(buf); return -1; }
        load = (uint16_t)(data[0] | (data[1] << 8));   /* little-endian in C64 data */
        data += 2;
        datalen -= 2;
    } else {
        load = load_hdr;
    }
    if (datalen < 0) datalen = 0;
    if (load + datalen > 0x10000) datalen = 0x10000 - load;
    memcpy(mem + load, data, (size_t)datalen);

    info->load_addr = load;
    if (info->init_addr == 0) info->init_addr = load;
    if (info->start_song == 0) info->start_song = 1;

    free(buf);
    return 0;
}
