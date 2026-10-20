/* ROC.DAT: the game's LHA archive (level-1 headers, -lh5- or -lh0- files, zero byte at the
 * end). The original reads it with AOPEN / AREAD (UAR.C) and Okumura's ar002 decoder (HUF.C,
 * DECODE.C, MAKETBL.C); this is the same format, read into memory in one go. */
#ifndef ROC_ARCHIVE_H
#define ROC_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[16];
    bool stored;        /* -lh0- (else -lh5-) */
    uint32_t offset;    /* of the packed data in the archive */
    uint32_t packed;
    uint32_t size;
    uint16_t crc;       /* CRC-16/ARC of the unpacked data */
} arc_entry_t;

typedef struct {
    uint8_t *data;
    size_t len;
    arc_entry_t *entries;
    int count;
} archive_t;

/* Read and index the archive. On failure returns false with a message in err. */
bool arc_open(archive_t *a, const char *path, char *err, size_t errlen);
void arc_close(archive_t *a);

/* Look a file up by name (case-insensitive, like DOS). */
const arc_entry_t *arc_find(const archive_t *a, const char *name);

/* Unpack a file and check its CRC. Returns a malloc'd buffer (free() it) and its size, or
 * NULL if the file is missing or damaged. */
uint8_t *arc_load(const archive_t *a, const char *name, size_t *len);

#endif
