# Credits

This repository is a fork of [mtoyoda/sl](https://github.com/mtoyoda/sl). The
`whistle/` directory is a steam-whistle variant of sl in C (`whistle/sl.c`). It copies code, art and ideas from
the projects below.

## mtoyoda/sl: the original

- <https://github.com/mtoyoda/sl>
- Copyright 1993, 1998, 2014 Toyoda Masashi
- The animation logic, the D51 / C51 / little locomotive ASCII art, the
  `-a`, `-l`, `-F` and `-c` options, and the whole idea. `whistle/sl.c` is a
  port of sl 5.02.
- Its license (see [LICENSE](LICENSE)) allows copying, modification and
  improvement as long as the copyright notice appears in all copies and you
  don't pretend you wrote it. The notice is kept in the header of
  `whistle/sl.c`.

## kerker00/sl: the TGV and ICE fork

- <https://github.com/kerker00/sl>
- Markus Mueller and the fork's other contributors.
- The `-G` (TGV), `-i` (ICE 1), `-3` (ICE 3 Redesign), `-b` (backwards) and
  `-L` (land) options, their long names, the train priority order, and the
  colors. The ICE art comes from the fork, which got it from
  [ascii.co.uk](https://ascii.co.uk).

## Debian's sl package

- <https://packages.debian.org/sl>
- The `-e` / `--interrupt` option (allow Ctrl+C).

## Added in this fork (Micah Fox)

- The steam whistle: synthesized WAV, played at full system volume through
  PulseAudio/PipeWire (`paplay` and `pactl`), with a lock so overlapping runs
  don't collide.
- Random stacking of options when run with no arguments.
- `-f` / `--files`: files in the current directory ride along as cars.
- Piped text becomes labeled passenger cars.
- A self-contained C port (`whistle/sl.c`) that needs only the C library and
  doesn't depend on ncurses or an installed sl.

These additions are MIT licensed, see [whistle/LICENSE](whistle/LICENSE).
