Video Game Music (VGM) player for low-end computers. The initial target is
8088 Tandy 1000 systems with Texas Instruments SN76496 or NCR 8496 3-voice
sound chips. Additional base systems and sound chips will likely be added in
the future.

There is a YouTube video showing the functionality of the 0.1 release:

https://youtu.be/yndzpGBLQZQ

To maximize current performance and memory footprint, and future portability,
C with small bits of assembly language will be used. Currently only Open
Watcom 1.9 is supported.

The player is currently able to play files smaller than 64k on DOSBox and real
hardware. On my Tandy 1000 HX (7.1MHz 8088), playback is slightly slow. The
track "Vampire Killer" from the DOS Castlevania should play back in 32s, but
it requires a little over 35s.

TODO:

- Add the ability to play files larger than 64k.

- Add the ability for the user to exit playback early.

- Enable support for song loops.

- Enable support for later Tandy 1000 models. These put the sound chip at IO
  port 0x1e0 instead of 0xc0. Add a command line option for the IO port. Is it
  possible to autodetect?

- Enable support for older header version (back to 101) so that Sega Master
  System VGMs from https://www.zophar.net/music/sega-master-system-vgm/ can be
  played.

- Enable support for dual SN76496 chips via ISA card add on. Many arcade games
  (i.e., most Sega System 1 games) used two of these chips.
