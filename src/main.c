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
#include <conio.h>
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

static inline uint8_t
get_uint8(struct vgm_buf *v)
{
    if (v->pos >= v->size)
        return 0x66;

    return v->buffer[v->pos++];
}

static inline uint16_t
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

static inline uint32_t
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

static int32_t adj_up;
static int32_t adj_dn;
static int32_t initial;
static uint8_t step;

/**
 * Wait for a number of 44.1kHz samples.
 *
 * This uses an interpolation method similar to a Bresenham run-slice line
 * drawing algorithm. This avoids mulitplication and division in the
 * time-critical code, and it maintains a decent level of accuracy.
 *
 * \note \c calibrate_delay must be called before calling this function.
 */
static void
wait_44khz(uint16_t samples)
{
    int32_t err = initial;
    int32_t remain = samples;

    while (remain > 0) {
        err += 2 * adj_up;
        if (err > 0) {
            err -= adj_dn;
            remain--;
        }

        remain -= step;
    }
}

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
        iterations++;
        third = get_tick();
    } while (third == second);

#ifdef DEBUG_LOG
    printf("iterations = %lu\n", (unsigned long) iterations);
#endif

    /* Refine the initial estimate by rerunning the loop without calling
     * get_tick inside the loop. Use the actual delay loop. This is done by
     * selecting parameter values that will fix the number of iterations
     * through the delay loop at FACTOR * iterations.
     *
     * These values are selected so that the if-statement inside the loop will
     * execute 50% of the time. This gives a more accurate representation of
     * the per-loop execution time.
     */
#define FACTOR 4ul

    step = 1;
    adj_up = 1;
    adj_dn = 4;
    initial = -2;

    iterations *= FACTOR / 2;

    do {
        iterations *= 2;

        third = get_tick();
        do {
            first = get_tick();
        } while (first == third);

        wait_44khz(3u * iterations / 2u);
        second = get_tick();
    } while ((second - first) < 4);

#ifdef DEBUG_LOG
    printf("%lu ticks for %lu iterations\n",
           (unsigned long)(second - first),
           (unsigned long)(iterations * 32));
#endif

    /* There are 1,573,040 ticks in a day. A day is 24h * 60m * 60s = 86,400
     * seconds. 1573040 / 86400 is the exact representation of the PC 18.2Hz
     * clock. That fraction reduces to 19663 / 1080.
     *
     * ticks * 19663 / 1080 = samples * 44100
     * ticks * 19663 / (1080 * 44100) = samples
     * ticks * 2809 / 6804000 = samples
     *
     * 1 tick = iterations / (second - first)
     */
    uint32_t n = 2809 * (iterations / FACTOR);
    uint32_t d = (second - first) * (6804000ul / FACTOR);
    assert(6804000ul % FACTOR == 0);

#ifdef DEBUG_LOG
    printf("%lu / %lu\n", n, d);
#endif

    adj_up = d % n;
    adj_dn = n * 2;
    step = d / n;
    initial = adj_up - adj_dn;

#ifdef DEBUG_LOG
    printf("Delay loop parameters: adj_up = %ld, adj_dn = %ld, initial = %ld, "
           "step = %u\n",
           adj_up, adj_dn, initial, step);
#endif
}

static void
sn76489_off(void)
{
    outp(0xc0, 0x9f);
    outp(0xc0, 0xbf);
    outp(0xc0, 0xdf);
    outp(0xc0, 0xff);
}

static void
pc_speaker_start(unsigned freq)
{
    uint16_t period = 0xfffe & (0x1234dcUL / freq);

    outp(0x43, 0xb6);
    outp(0x42, period & 0x00ff);
    outp(0x42, period >> 8);

    uint8_t al = inp(0x61);
    outp(0x61, al | 0x03);
}

static void
pc_speaker_stop(void)
{
    uint8_t al = inp(0x61);
    outp(0x61, al & 0xfc);
}

static void
play_Tandy_sound(struct vgm_buf *v, struct vgm_header *header)
{
    bool done = false;

    /* AY-8910 channel A period */
    uint16_t period = 0;

    while (!done) {
        uint8_t command = get_uint8(v);

        switch (command) {
        case 0x30: /* reserved one-byte command. */
        case 0x31: /* AY8910 stereo mask */
        case 0x32: /* reserved one-byte command. */
        case 0x33: /* reserved one-byte command. */
        case 0x34: /* reserved one-byte command. */
        case 0x35: /* reserved one-byte command. */
        case 0x36: /* reserved one-byte command. */
        case 0x37: /* reserved one-byte command. */
        case 0x38: /* reserved one-byte command. */
        case 0x39: /* reserved one-byte command. */
        case 0x3a: /* reserved one-byte command. */
        case 0x3b: /* reserved one-byte command. */
        case 0x3c: /* reserved one-byte command. */
        case 0x3d: /* reserved one-byte command. */
        case 0x3e: /* reserved one-byte command. */
        case 0x3f: /* reserved one-byte command. */
        case 0x4f: /* Game Gear PSG stereo */
        case 0x94: /* Stop stream */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 1);
            break;

        case 0x40: /* Mikey write */
        case 0x41: /* reserved two-byte command. */
        case 0x42: /* reserved two-byte command. */
        case 0x43: /* reserved two-byte command. */
        case 0x44: /* reserved two-byte command. */
        case 0x45: /* reserved two-byte command. */
        case 0x46: /* reserved two-byte command. */
        case 0x47: /* reserved two-byte command. */
        case 0x48: /* reserved two-byte command. */
        case 0x49: /* reserved two-byte command. */
        case 0x4a: /* reserved two-byte command. */
        case 0x4b: /* reserved two-byte command. */
        case 0x4c: /* reserved two-byte command. */
        case 0x4d: /* reserved two-byte command. */
        case 0x4e: /* reserved two-byte command. */
        case 0x51: /* YM2413 write */
        case 0x52: /* YM2612 port 0 write */
        case 0x53: /* YM2612 port 1 write */
        case 0x54: /* YM2151 write */
        case 0x55: /* YM2203 write */
        case 0x56: /* YM2608 port 0 write */
        case 0x57: /* YM2608 port 1 write */
        case 0x58: /* YM2610 port 0 write */
        case 0x59: /* YM2610 port 1 write */
        case 0x5a: /* YM3812 write */
        case 0x5b: /* YM3526 write */
        case 0x5c: /* Y8950 write */
        case 0x5d: /* YMZ280B write */
        case 0x5e: /* YMF262 port 0 write */
        case 0x5f: /* YMF262 port 1 write */
        case 0xa1: /* YM2413 write (second chip) */
        case 0xa2: /* YM2612 port 0 write (second chip) */
        case 0xa3: /* YM2612 port 1 write (second chip) */
        case 0xa4: /* YM2151 write (second chip) */
        case 0xa5: /* YM2203 write (second chip) */
        case 0xa6: /* YM2608 port 0 write (second chip) */
        case 0xa7: /* YM2608 port 1 write (second chip) */
        case 0xa8: /* YM2610 port 0 write (second chip) */
        case 0xa9: /* YM2610 port 1 write (second chip) */
        case 0xaa: /* YM3812 write (second chip) */
        case 0xab: /* YM3526 write (second chip) */
        case 0xac: /* Y8950 write (second chip) */
        case 0xad: /* YMZ280B write (second chip) */
        case 0xae: /* YMF262 port 0 write (second chip) */
        case 0xaf: /* YMF262 port 1 write (second chip) */
        case 0xb0: /* RF5C68 write */
        case 0xb1: /* RF5C164 write */
        case 0xb2: /* PWM write */
        case 0xb3: /* GameBoy DMG write */
        case 0xb4: /* NES APU write */
        case 0xb5: /* MultiPCM write */
        case 0xb6: /* uPD7759 write */
        case 0xb7: /* OKIM6258 write */
        case 0xb8: /* OKIM6295 write */
        case 0xb9: /* HuC6280 write */
        case 0xba: /* K053260 write */
        case 0xbb: /* Pokey write */
        case 0xbc: /* WonderSwan write */
        case 0xbd: /* SAA1099 write */
        case 0xbe: /* ES5506 write */
        case 0xbf: /* GA20 write */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 2);
            break;

        case 0xc0: /* Sega PCM write */
        case 0xc1: /* RF5C68 write */
        case 0xc2: /* RF5C164 write */
        case 0xc3: /* MultiPCM write */
        case 0xc4: /* QSound write */
        case 0xc5: /* SCSP write */
        case 0xc6: /* WonderSwan write */
        case 0xc7: /* VSU write */
        case 0xc8: /* X1-010 write */
        case 0xc9: /* reserved three-byte command. */
        case 0xca: /* reserved three-byte command. */
        case 0xcb: /* reserved three-byte command. */
        case 0xcc: /* reserved three-byte command. */
        case 0xcd: /* reserved three-byte command. */
        case 0xce: /* reserved three-byte command. */
        case 0xcf: /* reserved three-byte command. */
        case 0xd0: /* YMF278B port write */
        case 0xd1: /* YMF271 port write */
        case 0xd2: /* SCC1 port write */
        case 0xd3: /* K054539 write */
        case 0xd4: /* C140 write */
        case 0xd5: /* ES5503 write */
        case 0xd6: /* ES5506 write */
        case 0xd7: /* reserved three-byte command. */
        case 0xd8: /* reserved three-byte command. */
        case 0xd9: /* reserved three-byte command. */
        case 0xda: /* reserved three-byte command. */
        case 0xdb: /* reserved three-byte command. */
        case 0xdc: /* reserved three-byte command. */
        case 0xdd: /* reserved three-byte command. */
        case 0xde: /* reserved three-byte command. */
        case 0xdf: /* reserved three-byte command. */
        case 0xe1: /* C352 write */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 3);
            break;

        case 0xe0: /* Seek to offset in PCM data bank. */
        case 0xe2: /* reserved four-byte command. */
        case 0xe3: /* reserved four-byte command. */
        case 0xe4: /* reserved four-byte command. */
        case 0xe5: /* reserved four-byte command. */
        case 0xe6: /* reserved four-byte command. */
        case 0xe7: /* reserved four-byte command. */
        case 0xe8: /* reserved four-byte command. */
        case 0xe9: /* reserved four-byte command. */
        case 0xea: /* reserved four-byte command. */
        case 0xeb: /* reserved four-byte command. */
        case 0xec: /* reserved four-byte command. */
        case 0xed: /* reserved four-byte command. */
        case 0xee: /* reserved four-byte command. */
        case 0xef: /* reserved four-byte command. */
        case 0xf0: /* reserved four-byte command. */
        case 0xf1: /* reserved four-byte command. */
        case 0xf2: /* reserved four-byte command. */
        case 0xf3: /* reserved four-byte command. */
        case 0xf4: /* reserved four-byte command. */
        case 0xf5: /* reserved four-byte command. */
        case 0xf6: /* reserved four-byte command. */
        case 0xf7: /* reserved four-byte command. */
        case 0xf8: /* reserved four-byte command. */
        case 0xf9: /* reserved four-byte command. */
        case 0xfa: /* reserved four-byte command. */
        case 0xfb: /* reserved four-byte command. */
        case 0xfc: /* reserved four-byte command. */
        case 0xfd: /* reserved four-byte command. */
        case 0xfe: /* reserved four-byte command. */
        case 0xff: /* reserved four-byte command. */
        case 0x90: /* Setup stream control */
        case 0x91: /* Set stream data */
        case 0x95: /* Start stream (fast call) */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 4);
            break;

        case 0x92: /* Set stream frequency */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 5);
            break;

        case 0x93: /* Start stream */
            printf("command = 0x%02x\n", (unsigned) command);
            skip_bytes(v, 10);
            break;

        case 0x50: {
            /* SN76489 / SN76496 write */
            uint8_t d = get_uint8(v);

            outp(0xc0, d);
            break;
        }

        case 0x61:
            /* Wait n samples. n is 16-bit value. */
            wait_44khz(get_uint16(v));
            break;

        case 0x62:
            /* Wait 735 samples */
            wait_44khz(735);
            break;

        case 0x63:
            /* Wait 882 samples */
            wait_44khz(882);
            break;

        case 0x66:
            /* End of sound data. */
            done = true;
            break;

        case 0x67: {
            /* Data block. */
            printf("command = 0x%02x\n", (unsigned) command);

            /* Should be 0x66, followed by a byte for the data type. */
            uint8_t marker = get_uint8(v);
            if (marker != 0x66)
                goto parse_error;

            skip_bytes(v, 1);

            /* The next four bytes specify how much data follows. */
            skip_bytes(v, get_uint32(v));
            break;
        }

        case 0x68: {
            /* PCM RAM write. */
            printf("command = 0x%02x\n", (unsigned) command);

            /* Should be 0x66, followed by a byte for the chip type, and 12
             * bytes of offsets and sizes.
             */
            uint8_t marker = get_uint8(v);
            if (marker != 0x66)
                goto parse_error;

            skip_bytes(v, 13);
            break;
        }

        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7a:
        case 0x7b:
        case 0x7c:
        case 0x7d:
        case 0x7e:
        case 0x7f:
            /* Wait n+1 samples. */
            wait_44khz((command & 0x0f) + 1);
            break;

        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
            printf("command = 0x%02x\n", (unsigned) command);
            /* YM2612 port 0 write from data pointer, then wait. */
            break;

        case 0xa0: {
            /* AY8910 write */
            uint8_t v1 = get_uint8(v);
            uint8_t v2 = get_uint8(v);

            if (v1 == 0) {
                period = (period & 0xff00) | v2;
            } else if (v1 == 1) {
                period = 0x0fff & ((period & 0x00ff) | ((uint16_t)v2 << 8));

                /* The documentation for the AY-8910 says:
                 *
                 *    The frequence of each square wave generate by the three
                 *    Tone Generators ... is obtained in the PSG by first
                 *    counting down the input clock by 16, then by further
                 *    counting down the result by the programmed 12-bit Tone
                 *    Period value.
                 *
                 * This is not very clear to me. However, clk / (16 * period)
                 * seems to produce credible results.
                 */
                pc_speaker_start(header->ay8910_clock / (16 * period));
            } else if (v1 == 7) {
                if ((v2 & 1) != 0 && period != 0) {
                    pc_speaker_start(header->ay8910_clock / (16 * period));
                }
            } else if (v1 == 8) {
                if ((v2 & 1) == 0)
                    pc_speaker_stop();
            } else {
                printf("ay8910 - unsupported register 0x%02x\n", v1);
            }

            break;
        }

        default:
            printf("command = 0x%02x\n", (unsigned) command);
            goto parse_error;
        }
    }

    sn76489_off();
    pc_speaker_stop();
    return;

 parse_error:
    printf("parse error\n");
    sn76489_off();
    return;
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

    struct vgm_buf v = { buffer, size, 0 };

    calibrate_delay();

    play_Tandy_sound(&v, &header);

 fail:
    close(fd);
    return 0;
}
