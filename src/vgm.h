/*
 * Copyright 2024 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0
 */

#ifndef VGM_H
#define VGM_H

struct vgm_header {
    char ident[4];
    uint32_t eof_offset;
    uint32_t version;
    uint32_t sn76489_clock;
    uint32_t ym2314_clock;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    uint32_t loop_samples;
    uint32_t rate;
    uint16_t sn76489_fb;
    uint8_t  sn76489_fsr_width;
    uint8_t  sn76489_flags;
    uint32_t ym2612_clock;
    uint32_t ym2151_clock;
    uint32_t vgm_data_offset;
    uint32_t sega_pcm_clock;
    uint32_t spcm_interface;
    uint32_t rf5c68_clock;
    uint32_t ym2203_clock;
    uint32_t ym2608_clock;
    uint32_t ym2610_clock;
    uint32_t ym3812_clock;
    uint32_t ym3526_clock;
    uint32_t y8950_clock;
    uint32_t ymf262_clock;
    uint32_t ymf278b_clock;
    uint32_t ymf271_clock;
    uint32_t ymz280b_clock;
    uint32_t rf5c164_clock;
    uint32_t pwm_clock;
    uint32_t ay8910_clock;
    uint8_t  ay8910_type;
    uint8_t  ay8910_flags[3];
    uint8_t  volume_modifier;
    uint8_t  pad1;
    uint8_t  loop_base;
    uint8_t  loop_modifier;
    uint32_t gb_dmg_clock;
    uint32_t nes_apu_clock;
    uint32_t multipcm_clock;
    uint32_t uPD7759_clock;
    uint32_t okim6258_clock;
    uint8_t  of;
    uint8_t  kf;
    uint8_t  cf;
    uint8_t  pad2;
    uint32_t okim6295_clock;
    uint32_t k051649_clock;
    uint32_t k054539_clock;
    uint32_t HuC6280_clock;
    uint32_t c140_clock;
    uint32_t k053260_clock;
    uint32_t pokey_clock;
    uint32_t qsound_clock;
    uint32_t scsp_clock;
    uint32_t extra_header_offset;
    uint32_t wonderswan_clock;
    uint32_t vsu_clock;
    uint32_t saa1099_clock;
    uint32_t es5503_clock;
    uint32_t es5506_clock;
    uint16_t es_chns;
    uint8_t  cd;
    uint8_t  pad3;
    uint32_t x1_010_clock;
    uint32_t c352_clock;
    uint32_t ga20_clock;
    uint32_t mikey_clock;
    uint8_t  pad4[24];
};

struct gd3_header {
    char ident[4];
    uint32_t version;
    uint32_t length;
};

#endif /* ifndef VGM_H */
