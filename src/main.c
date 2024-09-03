/*
 * Copyright 2024 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
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

    static const char ident[4] = { 'V', 'g', 'm', ' ' };
    if (memcmp(header.ident, ident, sizeof(ident)) != 0) {
        printf("Header identifier does not match expected value.\n");

        if (header.ident[0] == (char)0x1f &&
            header.ident[1] == (char)0x8b) {
            printf("File appears to be GZIP data. This player cannot handle "
                   "VGZ files.\n");
        }

        goto fail;
    }

    printf("header version = %x\n", header.version);

    if (header.version < 0x150) {
        printf("Header version too old. At least 150 is required.\n");
        goto fail;
    }

    if (header.version < 0x151) {
        header.sn76489_flags = 0;
        header.ay8910_clock = 0;
    }

    printf("SN76489 clock = %lu\n", (unsigned long)header.sn76489_clock);
    printf("SN76489 feedback = 0x%x\n", header.sn76489_fb);
    printf("SN76489 FSR width = %d\n", header.sn76489_fsr_width);
    printf("SN76489 flags = 0x%x\n", header.sn76489_flags);

    if (header.ay8910_clock != 0) {
        printf("AY-8910 clock = %lu\n", (unsigned long)header.ay8910_clock);
        printf("AY-8910 chip type = %d\n", header.ay8910_type);
        printf("AY-8910 flags = 0x%02x 0x%02x 0x%02x\n",
               header.ay8910_flags[0],
               header.ay8910_flags[1],
               header.ay8910_flags[2]);

        /* The only VGM files that I have observed with this quirk are
         * from the Tandy 1000 version of Castlevania.
         */
        printf("\nAY-8910 is assumed to be placeholder for PC speaker.\n");
    }

#define VALIDATE_CHIP(clk, name)					   \
    do {								   \
        if (header. clk != 0)						   \
            printf("Sound chip %s not supported by this player.\n", name); \
    } while (false)

    VALIDATE_CHIP(ym2612_clock, "YM2612");
    VALIDATE_CHIP(ym2151_clock, "YM2151");

    if (header.version >= 0x151) {
        VALIDATE_CHIP(sega_pcm_clock, "Sega PCM");
        VALIDATE_CHIP(rf5c68_clock, "RF5C68");
        VALIDATE_CHIP(ym2203_clock, "YM2203");
        VALIDATE_CHIP(ym2608_clock, "YM2608");
        VALIDATE_CHIP(ym2610_clock, "YM2610");
        VALIDATE_CHIP(ym3812_clock, "YM3812");
        VALIDATE_CHIP(ym3526_clock, "YM3526");
        VALIDATE_CHIP(y8950_clock, "Y8950");
        VALIDATE_CHIP(ymf262_clock, "YMF262");
        VALIDATE_CHIP(ymf278b_clock, "YMF278b");
        VALIDATE_CHIP(ymf271_clock, "YMF271");
        VALIDATE_CHIP(ymz280b_clock, "YMZ280b");
        VALIDATE_CHIP(rf5c164_clock, "RF5C164");
        VALIDATE_CHIP(pwm_clock, "PWM");
    }

    if (header.version >= 0x161) {
        VALIDATE_CHIP(gb_dmg_clock, "Gameboy DMG");
        VALIDATE_CHIP(nes_apu_clock, "NES APU");
        VALIDATE_CHIP(multipcm_clock, "Multi PCM");
        VALIDATE_CHIP(uPD7759_clock, "uPD7759");
        VALIDATE_CHIP(okim6258_clock, "OKIM6258");
        VALIDATE_CHIP(okim6295_clock, "OKIM6295");
        VALIDATE_CHIP(k051649_clock, "K051649");
        VALIDATE_CHIP(k054539_clock, "K054539");
        VALIDATE_CHIP(HuC6280_clock, "HuC6280");
        VALIDATE_CHIP(c140_clock, "C140");
        VALIDATE_CHIP(k053260_clock, "K053260");
        VALIDATE_CHIP(pokey_clock, "Pokey");
        VALIDATE_CHIP(qsound_clock, "Qsound");
    }

    if (header.version >= 0x171) {
        VALIDATE_CHIP(scsp_clock, "SCSP");
        VALIDATE_CHIP(wonderswan_clock, "WonderSwan");
        VALIDATE_CHIP(vsu_clock, "VSU");
        VALIDATE_CHIP(saa1099_clock, "SAA1099");
        VALIDATE_CHIP(es5503_clock, "ES5503");
        VALIDATE_CHIP(es5506_clock, "ES5506");
        VALIDATE_CHIP(x1_010_clock, "X1-010");
        VALIDATE_CHIP(c352_clock, "C352");
        VALIDATE_CHIP(ga20_clock, "GA20");
    }

    if (header.version >= 0x172) {
        VALIDATE_CHIP(mikey_clock, "Mikey");
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
