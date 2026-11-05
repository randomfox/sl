SL(1): Cure your bad habit of mistyping
=======================================

SL (Steam Locomotive) runs across your terminal when you type "sl" as
you meant to type "ls". It's just a joke command, and not useful at
all.

Copyright 1993,1998,2014 Toyoda Masashi (mtoyoda@acm.org)

![](demo.gif)

---

## This fork: sl with a steam whistle

This repository is a fork of [mtoyoda/sl](https://github.com/mtoyoda/sl).
The original `sl.c` is unchanged. The addition is [`whistle/sl.c`](whistle/sl.c):
sl in C with a steam whistle, the TGV/ICE trains from kerker00's fork, `-f`
(files ride along as cars) and piped-text cars. It needs PulseAudio or PipeWire
(`paplay`, `pactl`) for the whistle. Build it with:

    gcc -O2 -Wall -o whistle/sl whistle/sl.c -lm

The additions are MIT licensed ([whistle/LICENSE](whistle/LICENSE)). The code
and art derived from sl stay under the original terms in [LICENSE](LICENSE).
See [CREDITS.md](CREDITS.md) for the projects this fork copies from:
[mtoyoda/sl](https://github.com/mtoyoda/sl),
[kerker00/sl](https://github.com/kerker00/sl) and Debian's sl package.
