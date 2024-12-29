Video Game Music (VGM) player for low-end PC compatible computers. Playback is
currently only supported on systems with Texas Instruments SN76496 or NCR 8496
3-voice programmable sound generator (PSG). For vintage machines, this means
IBM PCjr and the Tandy 1000 line. Systems with add-on boards such as Lo-Tech
Tandy Sound Card or PicoGUS (in Tandy 1000 mode) are also supported.

There is a YouTube video showing the functionality of the 0.1 release:

    https://youtu.be/yndzpGBLQZQ

Limitations:

    - Only uncompressed VGM files are supported.

    - Only VGM files less than 64k are supported.

    - Song loops are not supported.

    - There is no way to terminate playback early.

Usage:

    vgmplay.exe [/delay:####:####] [/psg:###] [/mode:NAME] filename.vgm

Parameters listed in [] are optional.

Optional parameters are processed in order, and parameters appearing later on
the command line may override values set by parameters appearing earlier. For
example,

    vgmplay.exe /psg:1e0 /mode:tandy1000hx song.vgm

Will set the PSG base IO address to C0 via the /mode parameter.

Optional parameters:

        /delay:####:####

                Specify delay loop control parameters. The parameters are two
                numbers between 1 and 32767 (inclusive). /delay:289:270 works
                well on an unmodified Tandy 1000HX running in "fast" mode.

        /mode:NAME

                Specify an output mode. May set PSG IO address (as /psg and
                default delay parameters (as /delay). Currently supported
                modes are:

                tandy - Sets /psg:c0. This is the default mode.

                tandy1000, pcjr - Sets /psg:c0 and /delay:1027:540. These
                    delay parameters are based on 4.77MHz 8088 CPU. Parameters
                    for NEC V20 CPU may be different.

                tandy1000ex, tandy1000hx, tandy1000sx - Sets /psg:c0 and
                    /delay:289:270. These delay parameters are based on 7.1MHz
                    8088 CPU. Parameters for NEC V20 CPU may be different.

                tandy1000sl, tandy1000sl2, tandy1000rl, tandy1000tl3 - Sets
                    /psg:c0. Performance characteristics of these Tandy 1000
                    machines are unknown, so the delay parameters are unset.

                tandy1000tx, tandy1000tl, tandy1000tl2 - Sets /psg:c0 and
                    /delay:6965:14919. These delay parameters are based on
                    8MHz 80286 CPU.

                tandy1000rlx - Sets /psg:1e0. Performance characteristics of
                    these Tandy 1000 machines are unknown, so the delay
                    parameters are unset.

                tandy1000rsx - Sets /psg:1e0 and /delay:101:1079. Performance
                    characteristics of the Tandy 1000RSX was estimated using a
                    generic 25MHz 386sx system with a PicoGUS.

                If /mode is used in conjunction with a second PSG installed
                via add-on board, /psg must also be specified for each
                PSG. For example, a Tandy 1000TL with a PicoGUS installed in
                Tandy 1000 mode would need:

                    /mode:tandy1000tl /psg:c0 /psg:2c0

        /psg:###

                Specify IO port address for SN76489 PSG (programmable sound
                generator). Default value C0 is for PCjr and all Tandy 1000
                except RLX and RSX. For Tandy 1000RLX and Tandy 1000RSX, use
                /psg:1e0. For PicoGUS and lo-tech.co.uk Tandy Sound Card in
                default configuration, use /psg:2c0.

                This parameter may be specified multiple times to set the base
                IO address of multiple PSG chips. In a Tandy 1000 with a
                Lo-Tech Tandy Sound Card installed at IO address 2C0, use

                    /psg:c0 /psg:2c0

                when playing a VGM that has data for two PSGs. Note that
                specifying the parameters in the opposite order will change
                which parts of the VGM are played on each PSG.
