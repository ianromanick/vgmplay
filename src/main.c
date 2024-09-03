/*
 * Copyright 2024 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include "vgm.h"

/*
 * \param gd3_offset True offset of the start of the GD3 block in the
 * file.
 */
static void
dump_gd3(int fd, uint32_t gd3_offset)
{
    off_t pos = lseek(fd, gd3_offset, SEEK_SET);
    if (pos == (off_t) -1) {
        printf("Could not seek to GD3 header.\n");
        return;
    }

    struct gd3_header header;

    size_t bytes = read(fd, &header, sizeof(header));
    if (bytes == (size_t)-1 || bytes < sizeof(header)) {
        printf("Could not read GD3 header.\n");
        return;
    }

    if (header.version != 0x00000100)
        printf("Unknown GD3 version %x\n", header.version);

    char *buf = malloc(header.length);
    if (buf == NULL) {
        printf("Could not allocate %d bytes for GD3 data.\n",
               header.length);
        return;
    }

    bytes = read(fd, buf, header.length);
    if (bytes == (size_t)-1 || bytes < header.length) {
        printf("Could not read %d bytes of GD3 data.\n",
               header.length);
        free(buf);
        return;
    }

    printf("\n--- Start of GD3 data ---\n");
    for (unsigned i = 0; i < bytes; i += 2) {
        if (buf[i + 1] == 0) {
            char c = buf[i] == 0 ? '\n' : buf[i];

            printf("%c", c);
        }
    }

    printf("--- End of GD3 data ---\n");
    free(buf);
    return;
}

int
main(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    struct vgm_header header;
    assert(sizeof(header) == 256);
    int fd = open(argv[1], O_RDONLY | O_BINARY);

    if (fd < 0) {
        printf("Could not open file \"%s\".\n", argv[1]);
        return -1;
    }

    size_t bytes = read(fd, &header, sizeof(header));
    if (bytes == (size_t)-1 || bytes < sizeof(header)) {
        printf("Could not read header from VGM file.\n"
               "Error = %s.\n"
               "Got %u bytes.\n",
               strerror(errno),
               bytes);
        goto fail;
    }

    if (header.gd3_offset != 0)
        dump_gd3(fd, header.gd3_offset + 0x14);

    off_t pos = lseek(fd, header.vgm_data_offset + 0x34, SEEK_SET);
    if (pos == (off_t) -1)
        goto fail;

 fail:
    close(fd);
    return 0;
}
