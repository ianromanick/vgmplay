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
#include <malloc.h>
#include <i86.h>
#include "vgm.h"

struct vgm_buf {
    uint8_t far *buffer;
    uint32_t size;
    uint32_t pos;
};

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

static void
skip_bytes(struct vgm_buf *v, unsigned bytes_to_skip)
{
    v->pos += bytes_to_skip;

    if (v->pos > v->size)
        v->pos = v->size;
}

static uint8_t
get_uint8(struct vgm_buf *v)
{
    if (v->pos >= v->size)
        return 0x66;

    return v->buffer[v->pos++];
}

static uint16_t
get_uint16(struct vgm_buf *v)
{
    if (v->pos + 2 > v->size) {
        v->pos = v->size;
        return 0;
    } else {
        uint16_t result = (uint16_t)v->buffer[v->pos] |
            ((uint16_t)v->buffer[v->pos + 1] << 8);

        v->pos += 2;

        return result;
    }
}

static uint32_t
get_uint32(struct vgm_buf *v)
{
    if (v->pos + 4 > v->size) {
        v->pos = v->size;
        return 0;
    } else {
        uint32_t result = (uint32_t)v->buffer[v->pos] |
            ((uint32_t)v->buffer[v->pos + 1] << 8) |
            ((uint32_t)v->buffer[v->pos + 2] << 16) |
            ((uint32_t)v->buffer[v->pos + 3] << 24);

        v->pos += 4;

        return result;
    }
}

#define DEBUG_LOG

static uint32_t
get_tick()
{
#if 1
    union REGS r;

    r.h.ah = 0;
    int86(0x1a, &r, &r);

    return r.w.dx | ((uint32_t) r.w.cx << 16);
#else
    /* Reading the tick counter directly from low memory does not seem to work
     * on DOSBox. I'm not sure if this a DOSBox problem or I'm just doing it
     * wrong.
     */
    return *(volatile unsigned long far *) 0x0000046CL;
#endif
}

static uint32_t iterations_for_44kHz;
static uint16_t junk1 = 11;
static uint16_t junk2 = 13;

#define DELAY_WORK(x)                           \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97;                                  \
    (x) *= 97

static void
calibrate_delay()
{
    uint32_t first = get_tick();
    uint32_t second;

    do {
        second = get_tick();
    } while (first == second);

    uint32_t third;
    uint32_t iterations = 0;

    /* Get a first estimate of the number of iterations per 18.2Hz tick. This
     * will be an underestimate due to the overhead of calling get_tick inside
     * the loop.
     */
    do {
        DELAY_WORK(junk1);

        iterations++;
        third = get_tick();
    } while (third == second);

#ifdef DEBUG_LOG
    printf("iterations = %lu\n", (unsigned long) iterations);
#endif

    first = get_tick();

#define FACTOR 12ul

    /* Refine the initial estimate by rerunning the loop without calling
     * get_tick inside the loop.
     */
    for (uint32_t i = 0; i < (iterations * FACTOR); i++) {
        DELAY_WORK(junk1);
    }

    second = get_tick();

#ifdef DEBUG_LOG
    printf("%lu ticks for %lu iterations\n",
           (unsigned long)(second - first),
           (unsigned long)(iterations * 32));
#endif

    /* x * 1573040 / 86400 = y * 44100
     * x * 19663 / 1080 = y * 44100
     * x * 19663 / (1080 * 44100) = y
     * x * 2809 / 6804000 = y
     *
     * x = (iterations * FACTOR) / (second - first)
     */
    uint32_t n = (FACTOR * 2809ul) * iterations;
    uint32_t d = (second - first) * 6804000ul;

#ifdef DEBUG_LOG
    printf("%lu / %lu\n", n, d);
#endif

    iterations_for_44kHz = n / d;

    if (iterations_for_44kHz == 0)
        iterations_for_44kHz = 1;

#ifdef DEBUG_LOG
    printf("%lu iterations for 44100kHz\n", iterations_for_44kHz);
#endif
}

/**
 * Wait for a number of 44.1kHz samples.
 *
 * \note \c calibrate_delay must be called before calling this function.
 */
static void
wait_44khz(uint16_t samples)
{
    for (uint16_t i = 0; i < samples; i++) {
        for (uint32_t j = 0; j < iterations_for_44kHz; j++) {
            DELAY_WORK(junk2);
        }
    }
}

static uint8_t tmp_buf[4096];

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


    off_t end_pos = lseek(fd, 0, SEEK_END);
    if (end_pos == (int32_t) -1)
        goto fail;

    off_t pos = lseek(fd, header.vgm_data_offset + 0x34, SEEK_SET);
    if (pos == (off_t) -1)
        goto fail;

    off_t size = end_pos - pos;
    if (size >= 0xffffUL) {
        printf("Files larger than 64k are not yet supported.\n");
        goto fail;
    }

    uint8_t far *buffer = _fmalloc(size);
    if (buffer == NULL) {
        printf("Could not allocate %lu bytes of memory.\n",
               (unsigned long) size);
        goto fail;
    }

    /* All of this is because there isn't a version of read() than can write
     * the data to a far pointer.
     */
    off_t total_read = 0;
    while (total_read < size) {
        off_t remain = size - total_read;

        if (remain > sizeof(tmp_buf))
            remain = sizeof(tmp_buf);

        size_t bytes_read = read(fd, tmp_buf, remain);
        if (bytes_read == (size_t) -1 || bytes_read == 0) {
            printf("Unable to read %u bytes from file.\n",
                   (unsigned) remain);
            goto fail;
        }

        _fmemcpy(&buffer[total_read], tmp_buf, bytes_read);

        total_read += bytes_read;
    }

//    struct vgm_buf v = { buffer, size, 0 };

    calibrate_delay();

    printf("Nonsense numbers to trick the compiler: %d %d\n", junk1, junk2);

 fail:
    close(fd);
    return 0;
}
